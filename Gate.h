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
		Done,
		DoneUnconsumedResult,
	};

	struct DependencyNode;

	using DependencyNodeIndex = BaseIndex<DependencyNode>;

	struct DependencyNode
	{
		static std::span<DependencyNode> GetPoolSpan();
#if !defined(NDEBUG)
		void OnReturnToPool()
		{
			assert(!task_);
		}
#endif
		TRefCountPoolPtr<BaseTask, BaseIndex<BaseTask>> task_;

		DependencyNodeIndex next_;
		DependencyNodeIndex& NextRef() { return next_; }
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
			return depending_.GetState().gate;
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
		ETaskState ResetStateOnEmpty(ETaskState new_state, bool inc_tag = false)
		{
			return depending_.SetOnEmpty(new_state, 
				inc_tag ? lock_free::ETagAction::Increment : lock_free::ETagAction::None).gate;
		}

		// return number of unblocked tasks
		uint32 Unblock(ETaskState new_state, TRefCountPtr<BaseTask>* out_first_ready_dependency = nullptr, bool inc_tag = false);

		bool AddDependencyInner(DependencyNode& node, const ETaskState required_state, const uint8 required_tag)
		{
			return depending_.Add(node, required_state, required_tag);
		}

		bool AddDependencyInner(DependencyNode& node, const ETaskState required_state)
		{
			return depending_.Add(node, required_state);
		}

	private:
		lock_free::Collection<DependencyNode, ETaskState> depending_;
	};

	using GateTag = BaseTag<Gate, uint8>;
}