#pragma once

#include "RefCount.h"
#include "RefCountPoolPtr.h"
#include "LockFree.h"

namespace ts
{
	class BaseTask;

	enum class ETaskState : uint8
	{
#if !defined(NDEBUG)
		Nonexistent_Pooled,
#endif
		PendingOrExecuting,
		PendingOrExecuting_NonBLocking,
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

		Index next_ = kInvalidIndex;
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

		auto GetInnerState() const
		{
			return depending_.GetState();
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

		// return number of unblocked tasks
		uint32 Unblock(ETaskState new_state, TRefCountPtr<BaseTask>* out_first_ready_dependency = nullptr);

		bool AddDependencyInner(DependencyNode& node, ETaskState required_state)
		{
			return depending_.Add(node, required_state);
		}

	private:
		lock_free::Collection<DependencyNode, ETaskState> depending_;
	};
}