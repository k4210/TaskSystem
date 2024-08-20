#include "BaseTask.h"
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

enum class ETaskState : uint8
{
#if !defined(NDEBUG)
	Nonexistent_Pooled,
#endif
	PendingOrExecuting,
	Done,
};

bool BaseTask::IsDone() const
{
	return depending_.GetGateState() == ETaskState::Done;
}

void BaseTask::Wait()
{
	while (!IsDone())
	{
		const bool executed = TaskSystem::ExecuteATask();
		if (!executed)
		{
			std::this_thread::yield();
		}
	}
}

BaseTask::BaseTask()
	: depending_(
#ifdef NDEBUG
		ETaskState::Done
#else
		ETaskState::Nonexistent_Pooled
#endif
	)
{}

struct TaskSystemGlobals
{
	Pool<BaseTask, 1024> task_pool_;
	Pool<DependencyNode, 4096> dependency_pool_;
	LockFree::Stack<BaseTask> ready_to_execute_;
	std::array<std::thread, 8> threads_;
	bool working_ = false;
};

static TaskSystemGlobals globals;

std::span<DependencyNode> DependencyNode::GetPoolSpan()
{
	return globals.dependency_pool_.GetPoolSpan();
}

void TaskSystem::StartWorkerThreads()
{
	globals.~TaskSystemGlobals();//Remove
	new (&globals) TaskSystemGlobals();

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
	std::cout << "Max used tasks: " << globals.task_pool_.GetMaxUsedNum() << std::endl;
	std::cout << "Max used dependency nodes: " << globals.dependency_pool_.GetMaxUsedNum() << std::endl;
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

void BaseTask::Execute()
{
	assert(depending_.GetGateState() == ETaskState::PendingOrExecuting);
	assert(!prerequires_);
	assert(function_);

	function_();
	function_ = nullptr;

	DependencyNode* head = nullptr;
	DependencyNode* tail = nullptr;
	auto HandleDependency = [&](DependencyNode& node)
		{
			if (!head)
			{
				head = &node;
			}
			tail = &node;
			node.task_->OnPrerequireDone();
			node.task_ = nullptr;
		};
	depending_.CloseAndConsume(ETaskState::Done, HandleDependency);

	if (head)
	{
		assert(tail);
		globals.dependency_pool_.ReturnChain(*head, *tail);
	}
}
#if !defined(NDEBUG)
void BaseTask::OnReturnToPool()
{
	assert(GetRefCount() == 0);
	depending_.SetFastOnEmpty(ETaskState::Nonexistent_Pooled);
}
#endif
void BaseTask::OnRefCountZero()
{
	const ETaskState state = depending_.GetGateState();
	assert(state != ETaskState::Nonexistent_Pooled);
	if (state == ETaskState::Done)
	{
		globals.task_pool_.Return(*this);
	}
}

void TaskSystem::OnReadyToExecute(BaseTask& task)
{
	assert(task.depending_.GetGateState() == ETaskState::PendingOrExecuting);
	task.AddRef();
	globals.ready_to_execute_.Push(task);
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

std::span<BaseTask> BaseTask::GetPoolSpan()
{
	return globals.task_pool_.GetPoolSpan();
}

TRefCountPtr<BaseTask> TaskSystem::InitializeTask(std::function<void()> function, std::span<BaseTask*> prerequiers, const char* debug_name)
{
	TRefCountPtr<BaseTask> task = globals.task_pool_.Acquire();
	task->debug_name_ = debug_name;
	task->function_ = std::move(function);
	task->depending_.SetFastOnEmpty(ETaskState::PendingOrExecuting);
	task->prerequires_.store(static_cast<uint16>(prerequiers.size()), std::memory_order_relaxed);

	if (!prerequiers.size())
	{
		OnReadyToExecute(*task);
		return task;
	}

	DependencyNode* node = nullptr;
	for (const TRefCountPtr<BaseTask>& prereq : prerequiers)
	{
		assert(prereq);

		if (!node)
		{
			node = &globals.dependency_pool_.Acquire();
			node->task_ = task;
		}

		const bool added = prereq->depending_.Add(*node, ETaskState::PendingOrExecuting);
		if (added)
		{
			node = nullptr;
		}
		else
		{
			task->OnPrerequireDone();
		}
	}
	if (node)
	{
		node->task_ = nullptr;
		globals.dependency_pool_.Return(*node);
	}

	return task;
}

TRefCountPtr<BaseTask> BaseTask::Then(std::function<void()> function, const char* debug_name)
{
	BaseTask* PreReq[] = { this };
	return TaskSystem::InitializeTask(function, PreReq, debug_name);
}