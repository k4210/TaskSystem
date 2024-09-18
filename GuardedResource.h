#pragma once

#include "Task.h"

class GuardedResourceBase : public TRefCounted<GuardedResourceBase>
{
protected:
	enum class EState { Locked, Unlocked };

	GuardedResourceBase() = default;

	//Return if this call locked it.
	bool TryLockAndEnqueue(BaseTask& node);
	bool TryLock(); 

	void TriggerAsyncExecution();

	bool UnlockIfNoneEnqueued();
private:
	void ExecuteAll();

	LockFree::Collection<BaseTask, EState> state_ = { EState::Unlocked };
	LockFree::Index reverse_head_ = LockFree::kInvalidIndex;
};

template<typename T>
class GuardedResource : public GuardedResourceBase
{
public:
	template<class... Args>
	GuardedResource(Args&&... args)
		: resource_(std::forward<Args>(args)...)
	{}

	template<typename F>
	auto UseWhenAvailible(F&& function LOCATION_PARAM) -> TRefCountPtr < Future<decltype(function(*(T*)0)) >>
	{
		using ReturnType = decltype(function(*(T*)0));

		bool locked_by_this = TryLock();
		if (!locked_by_this)
		{
			TRefCountPtr<BaseTask> task = TaskSystem::InitializeTaskInner([function = std::forward<F>(function),
				resource = TRefCountPtr<GuardedResource>(this)]([[maybe_unused]] BaseTask& task) mutable
				{
					if constexpr (std::is_void_v<ReturnType>)
					{
						std::invoke(function, resource->Get());
					}
					else
					{
						task.result_.Store(std::invoke(function, resource->Get()));
					}
					resource = nullptr;
				},
				{}, ETaskFlags::DontExecute LOCATION_PASS);
			locked_by_this = TryLockAndEnqueue(*task);
			if (locked_by_this)
			{
				TriggerAsyncExecution();
			}
			return task.Cast<GenericFuture>().Cast<Future<ReturnType>>();
		}

		TRefCountPtr<Future<ReturnType>> future = TaskSystem::MakeFuture<ReturnType>();
		if constexpr (std::is_void_v<ReturnType>)
		{
			function(Get());
			future->Done();
		}
		else
		{
			future->Done(function(Get()));
		}

		if (!UnlockIfNoneEnqueued())
		{
			TriggerAsyncExecution();
		}

		return future;
	}

private:
	T& Get() { return resource_; }

	T resource_;
};
