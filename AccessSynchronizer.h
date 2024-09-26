#pragma once

#include "Task.h"
#include <concepts>

struct AccessSynchronizer
{
	//returns prerequire, to insert
	TRefCountPtr<BaseTask> Sync(BaseTask& task)
	{
		task.AddRef();
		BaseTask* prev = last_task_.exchange(&task);
		return TRefCountPtr<BaseTask>(prev, false);
	}

	bool SyncIfAvailible(BaseTask& task)
	{
		BaseTask* expexted = nullptr;
		bool replaced = last_task_.compare_exchange_strong(expexted, &task);
		if (replaced)
		{
			task.AddRef();
		}
		return replaced;
	}

	void Release(BaseTask& task)
	{
		BaseTask* expexted = &task;
		bool replaced = last_task_.compare_exchange_strong(expexted, nullptr);
		if (replaced)
		{
			task.Release();
		}
	}

	DEBUG_CODE(thread_local static bool is_any_asset_locked_;)
private:
	std::atomic<BaseTask*> last_task_ = nullptr;
};

template<typename T>
concept SyncPtr = requires(T ptr)
{
	{ ptr->synchronizer_ } -> std::same_as<AccessSynchronizer&>;
};

template<SyncPtr TPtr>
struct AccessSynchronizerScope
{
	AccessSynchronizerScope(TPtr resource, TRefCountPtr<BaseTask> task)
		: resource_(std::move(resource)), task_(std::move(task))
	{
		assert(resource_);
		assert(task_);
		assert(!AccessSynchronizer::is_any_asset_locked_); //If any other asset is locked it means there is a risk of deadlock
		DEBUG_CODE(AccessSynchronizer::is_any_asset_locked_ = true;)
	}

	auto& Get() { return *resource_; }

	~AccessSynchronizerScope()
	{
		resource_->synchronizer_.Release(*task_);
		Gate* gate = task_->GetGate();
		assert(gate);
		assert(gate->GetState() == ETaskState::PendingOrExecuting || gate->GetState() == ETaskState::ReleasedDependencies);
		gate->Done(ETaskState::ReleasedDependencies);
		assert(AccessSynchronizer::is_any_asset_locked_);
		DEBUG_CODE(AccessSynchronizer::is_any_asset_locked_ = false;)
	}

private:
	TPtr resource_;
	TRefCountPtr<BaseTask> task_;
};