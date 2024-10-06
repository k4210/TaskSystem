#include "Task.h"
#include <iostream>
#include "CoroutineHandle.h"

namespace ts
{
	struct TaskSystemGlobals
	{
		Pool<BaseTask, kTaskPoolSize
#if THREAD_SMART_POOL
			, kWorkeThreadsNum, InitPoolSizePerThread(kTaskPoolSize), MaxPoolSizePerThread(kTaskPoolSize)
#endif
		> task_pool_;
		Pool<DependencyNode, kDepNodePoolSize
#if THREAD_SMART_POOL
			, kWorkeThreadsNum, InitPoolSizePerThread(kDepNodePoolSize), MaxPoolSizePerThread(kDepNodePoolSize)
#endif
		> dependency_pool_;
		Pool<GenericFuture, kFuturePoolSize
#if THREAD_SMART_POOL
			, kWorkeThreadsNum, InitPoolSizePerThread(kFuturePoolSize), MaxPoolSizePerThread(kFuturePoolSize)
#endif
		> future_pool_;
		lock_free::Stack<BaseTask> ready_to_execute_;
		std::array<lock_free::Stack<BaseTask>, 5>  ready_to_execute_named;

		std::array<std::thread, kWorkeThreadsNum> threads_;
		bool working_ = false;
		std::atomic<uint8> used_threads_ = 0;

		lock_free::Stack<BaseTask>& ReadyStack(ETaskFlags flag)
		{
			int32 counter = 0;
			for (ETaskFlags thread_name : { ETaskFlags::NamedThread1, ETaskFlags::NamedThread2, ETaskFlags::NamedThread3, ETaskFlags::NamedThread4, ETaskFlags::NamedThread5})
			{
				if (enum_has_any(flag, thread_name))
				{
					return ready_to_execute_named[counter];
				}
				counter++;
			}
			return ready_to_execute_;
		}
	};
	static TaskSystemGlobals globals;
	thread_local static BaseTask* current_task = nullptr;

	void BaseTask::OnUnblocked(TRefCountPtr<BaseTask> task, TRefCountPtr<BaseTask>* out_first_ready_dependency)
	{
		assert(task);
		assert(task->prerequires_);
		uint16 new_count = --task->prerequires_;
		if (!new_count)
		{
			if (!::enum_has_any(task->flag_, ETaskFlags::NameThreadMask) &&
				out_first_ready_dependency && !out_first_ready_dependency->IsValid())
			{
				*out_first_ready_dependency = std::move(task);
			}
			else
			{
				TaskSystem::OnReadyToExecute(std::move(task));
			}
		}
	}

	std::span<DependencyNode> DependencyNode::GetPoolSpan()
	{
		return globals.dependency_pool_.GetPoolSpan();
	}

	std::span<GenericFuture> GenericFuture::GetPoolSpan()
	{
		return globals.future_pool_.GetPoolSpan();
	}

	void GenericFuture::OnRefCountZero()
	{
		DEBUG_CODE(auto inner_state = gate_.GetInnerState();)
			const ETaskState state = gate_.GetState();
		assert(state != ETaskState::Nonexistent_Pooled);
		assert(gate_.IsEmpty());
		if (state == ETaskState::DoneUnconsumedResult)
		{
			// RefCount is zero, so no other thread has access to the task
			result_.Reset();
		}
		assert(!result_.HasValue());
		if (state != ETaskState::Done)
		{
			gate_.ResetStateOnEmpty(ETaskState::Done);
		}

		if (globals.future_pool_.BelonsTo(*this))
		{
			globals.future_pool_.Return(*this);
		}
		else
		{
			BaseTask& task = *static_cast<BaseTask*>(this);
			assert(globals.task_pool_.BelonsTo(task));
			assert(!task.function_);
			assert(state != ETaskState::PendingOrExecuting);
			globals.task_pool_.Return(task);
		}
	}

	thread_local uint16 t_worker_thread_idx = kInvalidIndex;

	void TaskSystem::StartWorkerThreads()
	{
		globals.working_ = true;

		auto loop_body = [](uint16 index)
			{
				t_worker_thread_idx = index;
				bool marked_as_used = false;
				while (true)
				{
					BaseTask* pop_task = globals.ready_to_execute_.Pop();
					TRefCountPtr<BaseTask> task(pop_task, false);
					if (task)
					{
						if (!marked_as_used)
						{
							marked_as_used = true;
							globals.used_threads_.fetch_add(1, std::memory_order_relaxed);
						}

						do
						{
							TRefCountPtr<BaseTask> next = nullptr;
							task->Execute(&next);
							task = std::move(next);
						} while (task);
					}
					else
					{
						if (marked_as_used)
						{
							marked_as_used = false;
							globals.used_threads_.fetch_sub(1, std::memory_order_relaxed);
						}

						if (globals.working_) [[likely]]
						{
							std::this_thread::yield();
						}
						else
						{
							break;
						}
					}
				}
			};

		uint16 index = 0;
		for (std::thread& thread : globals.threads_)
		{
			thread = std::thread(loop_body, index);
			index++;
		}
	}

	void TaskSystem::StopWorkerThreadsNoWait()
	{
		globals.working_ = false;
	}

	void TaskSystem::WaitForAllTasks()
	{
		while (globals.used_threads_)
		{
			std::this_thread::yield();
		}
#if DO_POOL_STATS
		globals.task_pool_.AssertEmpty();
#endif
	}

	void TaskSystem::WaitForWorkerThreadsToJoin()
	{
		for (std::thread& thread : globals.threads_)
		{
			thread.join();
		}
		assert(!globals.ready_to_execute_.Pop());
#if DO_POOL_STATS
		globals.task_pool_.AssertEmpty();
		std::cout << "Max used tasks: " << globals.task_pool_.GetMaxUsedNum() << std::endl;
		std::cout << "Max used futures: " << globals.future_pool_.GetMaxUsedNum() << std::endl;
		std::cout << "Max used dependency nodes: " << globals.dependency_pool_.GetMaxUsedNum() << std::endl;
#endif
	}

	bool TaskSystem::ExecuteATask(ETaskFlags flag, std::atomic<bool>& out_active)
	{
		BaseTask* task = globals.ReadyStack(flag).Pop();
		out_active.store(!!task, std::memory_order_relaxed);
		if (task)
		{
			task->Execute();
			task->Release();
		}
		return !!task;
	}

	uint32 Gate::Unblock(ETaskState new_state, TRefCountPtr<BaseTask>* out_first_ready_dependency)
	{
		assert(GetState() == ETaskState::PendingOrExecuting
			|| GetState() == ETaskState::PendingOrExecuting_NonBLocking);

		DependencyNode* head = nullptr;
		DependencyNode* tail = nullptr;
		uint16 chain_len = 0;
		auto handle_dependency = [&](DependencyNode& node)
			{
				if (!head)
				{
					head = &node;
				}
				tail = &node;
				BaseTask::OnUnblocked(std::move(node.task_).ToRefCountPtr(), out_first_ready_dependency);
				chain_len++;
			};

		ETaskState old_state = depending_.ConsumeAll(new_state, handle_dependency);
		assert(old_state == ETaskState::PendingOrExecuting
			|| old_state == ETaskState::PendingOrExecuting_NonBLocking);

		if (head)
		{
			assert(tail);
			globals.dependency_pool_.ReturnChain(*head, *tail, chain_len);
		}

		return chain_len;
	}

	void BaseTask::Execute(TRefCountPtr<BaseTask>* out_first_ready_dependency)
	{
		assert(gate_.GetState() == ETaskState::PendingOrExecuting);
		assert(!prerequires_);
		assert(function_);
		assert(!current_task);
		assert(GetRefCount());

		current_task = this;
		function_(*this);
		current_task = nullptr;
		assert(GetRefCount());
#if TASK_RETRIGGER
		if (retrigger_)
		{
			retrigger_ = false;
			return;
		}
#endif
		const ETaskState new_state = result_.HasValue() ? ETaskState::DoneUnconsumedResult : ETaskState::Done;
		gate_.Unblock(new_state, out_first_ready_dependency);

		function_ = nullptr; //Moved to the end, because of InitializeTaskOn::LambdaObj
		assert(!function_);
		assert(GetRefCount());
	}

	void TaskSystem::OnReadyToExecute(TRefCountPtr<BaseTask> task)
	{
		assert(task->gate_.GetState() == ETaskState::PendingOrExecuting);
		globals.ReadyStack(task->flag_).Push(*task);
		task.ResetNoRelease();
	}

	std::span<BaseTask> BaseTask::GetPoolSpan()
	{
		return globals.task_pool_.GetPoolSpan();
	}

	TRefCountPtr<GenericFuture> TaskSystem::MakeGenericFuture()
	{
		TRefCountPtr<GenericFuture> future = globals.future_pool_.Acquire();
		assert(future->gate_.IsEmpty());
		const ETaskState old_state = future->gate_.ResetStateOnEmpty(ETaskState::PendingOrExecuting);
		assert(old_state == ETaskState::Nonexistent_Pooled);
		return future;
	}

	TRefCountPtr<BaseTask> TaskSystem::CreateTask(std::move_only_function<void(BaseTask&)> function, ETaskFlags flags
		LOCATION_PARAM_IMPL)
	{
		TRefCountPtr<BaseTask> task = globals.task_pool_.Acquire();
		assert(task);
		assert(!task->function_);
		DEBUG_CODE(task->source = location;)
			task->flag_ = flags;
		task->function_ = std::move(function);
		assert(task->gate_.IsEmpty());
		const ETaskState old_state = task->gate_.ResetStateOnEmpty(ETaskState::PendingOrExecuting);
		assert(old_state == ETaskState::Nonexistent_Pooled);
		assert(task->prerequires_ == 0);
		return task;
	}

	void TaskSystem::HandlePrerequires(BaseTask& task, std::span<Gate*> prerequiers)
	{
		task.prerequires_.store(static_cast<uint16>(prerequiers.size()), std::memory_order_relaxed);

		auto ProcessOnReady = [&]()
			{
				if (enum_has_any(task.GetFlags(), ETaskFlags::TryExecuteImmediate))
				{
					task.Execute();
				}
				else
				{
					OnReadyToExecute(task);
				}
			};

		if (!prerequiers.size())
		{
			ProcessOnReady();
			return;
		}

		uint16 inactive_prereq = 0;
		DependencyNode* node = nullptr;
		for (Gate* prereq : prerequiers)
		{
			if (!prereq || (prereq->GetState() != ETaskState::PendingOrExecuting))
			{
				inactive_prereq++;
				continue;
			}

			if (!node)
			{
				node = &globals.dependency_pool_.Acquire();
				node->task_ = task;
			}

			const bool added = prereq->AddDependencyInner(*node, ETaskState::PendingOrExecuting);
			if (added)
			{
				node = nullptr;
			}
			else
			{
				inactive_prereq++;
			}
		}
		if (node)
		{
			node->task_ = nullptr;
			globals.dependency_pool_.Return(*node);
		}

		if (inactive_prereq)
		{
			const uint16 before = task.prerequires_.fetch_sub(inactive_prereq);
			if (before == inactive_prereq)
			{
				assert(task.prerequires_ == 0);
				ProcessOnReady();
			}
		}

	}

	void TaskSystem::AsyncResume(DetachHandle handle LOCATION_PARAM_IMPL)
	{
		InitializeTask([handle = handle.Detach()]()
			{
				handle.resume();
			}, {}, ETaskFlags::None LOCATION_PASS);
	}

	BaseTask* BaseTask::GetCurrentTask()
	{
		return current_task;
	}
}