#include "Task.h"
#include <thread>
#include <iostream>

struct DependencyNode
{
	static std::span<DependencyNode> GetPoolSpan();
#if !defined(NDEBUG)
	void OnReturnToPool()
	{
		assert(!task_);
	}
#endif
	TRefCountPoolPtr<BaseTask> task_;

	LockFree::Index next_ = LockFree::kInvalidIndex;
};

uint16 BaseTask::GetNumberOfPendingPrerequires() const
{
	return prerequires_.load(std::memory_order_relaxed);
}

void BaseTask::OnPrerequireDone()
{
	assert(prerequires_);
	uint16 new_count = --prerequires_;
	if (!new_count)
	{
		TaskSystem::OnReadyToExecute(*this);
	}
}

struct TaskSystemGlobals
{
	Pool<BaseTask, 1024> task_pool_;
	Pool<DependencyNode, 4096> dependency_pool_;
	Pool<GenericFuture, 1024> future_pool_;
	LockFree::Stack<BaseTask> ready_to_execute_;
	std::array<std::thread, 16> threads_;
	bool working_ = false;
};

static TaskSystemGlobals globals;

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
	OnDestroy();
	globals.future_pool_.Return(*this);
}

void BaseTask::OnRefCountZero()
{
	OnDestroy();
	globals.task_pool_.Return(*this);
}

void TaskSystem::ResetGlobals()
{
	globals.~TaskSystemGlobals();
	new (&globals) TaskSystemGlobals();
}

void TaskSystem::StartWorkerThreads()
{
	globals.working_ = true;

	auto loop_body = []()
	{
		while (true)
		{
			const bool executed = ExecuteATask();
			if(!executed)
			{
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

	for (std::thread& thread : globals.threads_)
	{
		thread = std::thread(loop_body);
	}
}

void TaskSystem::StopWorkerThreads()
{
	globals.working_ = false;
}

void TaskSystem::WaitForWorkerThreadsToJoin()
{
	for (std::thread& thread : globals.threads_)
	{
		thread.join();
	}
#if DO_POOL_STATS
	std::cout << "Max used tasks: "  << globals.task_pool_.GetMaxUsedNum() << std::endl;
	std::cout << "Max used dependency nodes: "<< globals.dependency_pool_.GetMaxUsedNum() << std::endl;
#endif
}

bool TaskSystem::ExecuteATask()
{
	BaseTask* task = globals.ready_to_execute_.Pop();
	if (task)
	{
		task->Execute();
		task->Release();
	}
	return !!task;
}

void Gate::Done(ETaskState new_state)
{
	assert(GetState() == ETaskState::PendingOrExecuting);
	assert(new_state == ETaskState::Done || new_state == ETaskState::DoneUnconsumedResult);

	DependencyNode* head = nullptr;
	DependencyNode* tail = nullptr;
	auto handle_dependency = [&](DependencyNode& node)
		{
			if (!head)
			{
				head = &node;
			}
			tail = &node;
			node.task_->OnPrerequireDone();
			node.task_ = nullptr;
		};
	
	depending_.CloseAndConsume(new_state, handle_dependency);

	if (head)
	{
		assert(tail);
		globals.dependency_pool_.ReturnChain(*head, *tail);
	}
}

bool Gate::AddDependencyInner(DependencyNode& node, ETaskState required_state)
{
	return depending_.Add(node, required_state);
}

bool Gate::TryAddDependency(BaseTask& task)
{
	DependencyNode& node = globals.dependency_pool_.Acquire();
	node.task_ = task;

	const bool added = AddDependencyInner(node, ETaskState::PendingOrExecuting);
	if (!added)
	{
		node.task_ = nullptr;
		globals.dependency_pool_.Return(node);
	}
	return added;
}

void BaseTask::Execute()
{
	assert(gate_.GetState() == ETaskState::PendingOrExecuting);
	assert(!prerequires_);
	assert(function_);

	function_(*this);
	function_ = nullptr;

	const ETaskState new_state = result_.HasValue() ? ETaskState::DoneUnconsumedResult : ETaskState::Done;
	gate_.Done(new_state);
}

void TaskSystem::OnReadyToExecute(BaseTask& task)
{
	assert(task.gate_.GetState() == ETaskState::PendingOrExecuting);
	task.AddRef();
	globals.ready_to_execute_.Push(task);
}

std::span<BaseTask> BaseTask::GetPoolSpan()
{
	return globals.task_pool_.GetPoolSpan();
}

TRefCountPtr<BaseTask> TaskSystem::InitializeTaskInner(std::function<void(BaseTask&)> function, std::span<Gate*> prerequiers, const char* debug_name, ETaskFlags flags)
{
	TRefCountPtr<BaseTask> task = globals.task_pool_.Acquire();
	task->debug_name_ = debug_name;
	task->function_ = std::move(function);
	assert(task->gate_.IsEmpty());
	const ETaskState old_state = task->gate_.ResetStateOnEmpty(ETaskState::PendingOrExecuting);
	assert(old_state == ETaskState::Nonexistent_Pooled);
	task->prerequires_.store(static_cast<uint16>(prerequiers.size()), std::memory_order_relaxed);

	auto ProcessOnReady = [&]()
	{
		if (enum_has_any(flags, ETaskFlags::TryExecuteImmediate))
		{
			task->Execute();
		}
		else
		{
			OnReadyToExecute(*task);
		}
	};

	if (!prerequiers.size())
	{
		ProcessOnReady();
		return task;
	}

	uint16 inactive_prereq = 0;
	DependencyNode* node = nullptr;
	for (Gate* prereq : prerequiers)
	{
		assert(prereq);

		if (!node)
		{
			if (prereq->GetState() != ETaskState::PendingOrExecuting)
			{
				inactive_prereq++;
				continue;
			}
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
		const uint16 before = task->prerequires_.fetch_sub(inactive_prereq);
		if (before == inactive_prereq)
		{
			assert(task->prerequires_ == 0);
			ProcessOnReady();
		}
	}

	return task;
}
