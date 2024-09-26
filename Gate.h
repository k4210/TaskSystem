#pragma once

#include "RefCount.h"
#include "RefCountPoolPtr.h"
#include "LockFree.h"

class BaseTask;

enum class ETaskState : uint8
{
#if !defined(NDEBUG)
	Nonexistent_Pooled,
#endif
	PendingOrExecuting,
	ReleasedDependencies, // still working
	Done,
	DoneUnconsumedResult,
};

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

struct Gate
{
	Gate()
		: depending_(
#ifdef NDEBUG
			ETaskState::Done
#else
			ETaskState::Nonexistent_Pooled
#endif
		)
	{}

	~Gate()
	{
		assert(IsEmpty());
	}

	ETaskState GetState() const
	{
		return depending_.GetGateState();
	}

	bool IsEmpty() const
	{
		return depending_.IsEmpty();
	}

	// returns previous state
	ETaskState ResetStateOnEmpty(ETaskState new_state)
	{
		return depending_.SetFastOnEmpty(new_state);
	}

	void Done(ETaskState new_state, TRefCountPtr<BaseTask>* out_first_ready_dependency = nullptr);

	bool AddDependencyInner(DependencyNode& node, ETaskState required_state)
	{
		return depending_.Add(node, required_state);
	}

private:
	LockFree::Collection<DependencyNode, ETaskState> depending_;
};