#include "GuardedResource.h"

bool GuardedResourceBase::TryLockAndEnqueue(BaseTask& node)
{
	node.AddRef();
	return state_.AddForceOpen(node, EState::Locked) == EState::Unlocked;
}

bool GuardedResourceBase::TryLock() //Return if this call locked it.
{
	return state_.ExchangeState(EState::Locked) == EState::Unlocked;
}

bool GuardedResourceBase::UnlockIfNoneEnqueued()
{
	assert(reverse_head_ == LockFree::kInvalidIndex);
	return state_.ChangeIfEmpty(EState::Unlocked);
}

void GuardedResourceBase::ExecuteAll()
{
	if (reverse_head_ == LockFree::kInvalidIndex)
	{
		auto store_reversed = [&](BaseTask& node)
			{
				node.next_ = reverse_head_;
				reverse_head_ = GetPoolIndex(node);
			};
		const EState prev = state_.CloseAndConsume(EState::Locked, store_reversed);
		assert(EState::Locked == prev);
	}

	while (reverse_head_ != LockFree::kInvalidIndex)
	{
		BaseTask& node = FromPoolIndex<BaseTask>(reverse_head_);
		reverse_head_ = node.next_;
		node.next_ = LockFree::kInvalidIndex;
		node.Execute();
		node.Release();
	}
}

void GuardedResourceBase::TriggerAsyncExecution()
{
	TaskSystem::InitializeTask([resource = TRefCountPtr<GuardedResourceBase>(this)]
		{
			do { resource->ExecuteAll(); } while (!resource->UnlockIfNoneEnqueued());
		});
}