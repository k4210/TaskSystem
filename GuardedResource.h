#pragma once

#include "Task.h"
#include <type_traits>
/*
template<typename T> struct ResourceAccessScope;

class GuardedResourceBase : public TRefCounted<GuardedResourceBase>
{
public:
	bool TryLock();
	bool IsLocked() const
	{
		return state_.GetGateState() == EState::Locked;
	}
protected:
	enum class EState { Locked, Unlocked };

	GuardedResourceBase() = default;

	//Return if this call locked it.
	bool TryLockAndEnqueue(BaseTask& node);

	void TriggerAsyncExecution();

	bool UnlockIfNoneEnqueued();

	void ContinueSharing()
	{
		if (!UnlockIfNoneEnqueued())
		{
			TriggerAsyncExecution();
		}
	}

private:
	enum class EExecutionState
	{
		Done,
		Redirected
	};
	EExecutionState ExecuteAll();

	LockFree::Collection<BaseTask, EState> state_ = { EState::Unlocked };
	LockFree::Index reverse_head_ = LockFree::kInvalidIndex;

	template<typename T> friend struct ResourceAccessScope;
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
			TRefCountPtr<BaseTask> task = TaskSystem::CreateTask([function = std::forward<F>(function),
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
				ETaskFlags::None LOCATION_PASS);
			// Dont call HandlePrerequires
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

		ContinueSharing();

		return future;
	}

	template<typename F>
	void RedirectWhenAvailible(F&& function LOCATION_PARAM)
	{
		TRefCountPtr<BaseTask> task = TaskSystem::CreateTask(
			[function = std::forward<F>(function)](BaseTask&) { function(); },
			ETaskFlags::RedirectExecutrionForGuardedResource LOCATION_PASS);
		// Dont call HandlePrerequires
		const bool locked_by_this = TryLockAndEnqueue(*task);
		if (locked_by_this)
		{
			TriggerAsyncExecution();
		}
	}

private:
	T& Get() { return resource_; }

	T resource_;

	friend ResourceAccessScope<T>;
};

template<typename T>
struct ResourceAccessScope
{
	ResourceAccessScope(TRefCountPtr<GuardedResource<T>> locked_resource)
		: resource_(std::move(locked_resource))
	{
		assert(resource_ && resource_->IsLocked());
	}

	ResourceAccessScope(ResourceAccessScope&& other)
		: resource_(std::move(other.resource_))
	{}
	
	T& Get() { return resource_->Get(); }

	~ResourceAccessScope()
	{
		if (resource_)
		{
			resource_->ContinueSharing();
		}
	}

private:
	TRefCountPtr<GuardedResource<T>> resource_;
};
*/