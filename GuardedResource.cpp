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
	if (reverse_head_ != LockFree::kInvalidIndex)
	{
		return false;
	}
	return state_.ChangeIfEmpty(EState::Unlocked);
}

GuardedResourceBase::EExecutionState GuardedResourceBase::ExecuteAll()
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
		const bool redirected = enum_has_any(node.GetFlags(), ETaskFlags::RedirectExecutrionForGuardedResource);
		node.Release();
		if (redirected)
		{
			return EExecutionState::Redirected;
		}
	}
	return EExecutionState::Done;
}

void GuardedResourceBase::TriggerAsyncExecution()
{
	TaskSystem::InitializeTask([resource = TRefCountPtr<GuardedResourceBase>(this)]
		{
			do 
			{ 
				const EExecutionState state = resource->ExecuteAll();
				if (state == EExecutionState::Redirected)
				{
					return;
				}
			} while (!resource->UnlockIfNoneEnqueued());
		});
}