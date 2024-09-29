#pragma once

#include "BaseTask.h"
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
	AccessSynchronizerScope(TPtr resource) 
		: resource_(std::move(resource))
	{
		assert(resource_);
		assert(!AccessSynchronizer::is_any_asset_locked_); //If any other asset is locked it means there is a risk of deadlock
		DEBUG_CODE(AccessSynchronizer::is_any_asset_locked_ = true;)
	}

	auto& Get() { return *resource_; }

	~AccessSynchronizerScope()
	{
		BaseTask* local_current_task = BaseTask::GetCurrentTask();
		assert(local_current_task);
		Gate* gate = local_current_task->GetGate();
		assert(gate);
		assert(gate->GetState() == ETaskState::PendingOrExecuting 
			|| gate->GetState() == ETaskState::ReleasedDependencies);
		assert(AccessSynchronizer::is_any_asset_locked_);
		DEBUG_CODE(AccessSynchronizer::is_any_asset_locked_ = false;)
		const uint32 unblocked = gate->Done(ETaskState::ReleasedDependencies);
		assert(unblocked <= 1);
		resource_->synchronizer_.Release(*local_current_task);
		assert(gate->IsEmpty());
		gate->Done(ETaskState::ReleasedDependencies); // Just in case
	}

private:
	TPtr resource_;
};