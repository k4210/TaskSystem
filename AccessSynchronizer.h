#pragma once

#include "BaseTask.h"
#include <concepts>
#include <coroutine>
namespace ts
{
	struct AccessSynchronizer
	{
		//returns prerequire, to insert
		utils::TRefCountPtr<BaseTask> Sync(BaseTask& task)
		{
			task.AddRef();
			BaseTask* prev = last_task_.exchange(&task);
			assert(prev != &task);
			assert(!prev || (prev->GetRefCount() > 1) || prev->GetGate()->IsEmpty());
			return utils::TRefCountPtr<BaseTask>(prev, false);
		}

		bool SyncIfAvailible(BaseTask& task)
		{
			BaseTask* expexted = nullptr;
			task.AddRef();
			const bool replaced = last_task_.compare_exchange_strong(expexted, &task);
			if (!replaced)
			{
				task.Release();
			}
			return replaced;
		}

		void Release(BaseTask& task)
		{
			BaseTask* expexted = &task;
			const bool replaced = last_task_.compare_exchange_strong(expexted, nullptr);
			assert(!replaced || task.GetRefCount() > 1);
			if (replaced)
			{
				task.Release();
			}
		}

		bool IsLocked() const
		{
			return !!last_task_.load(std::memory_order_relaxed);
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
	struct SyncHolder
	{
		SyncHolder(TPtr ptr) : ptr_(ptr) {}
		SyncHolder(const SyncHolder&) = default;
		SyncHolder(SyncHolder&&) = default;
		SyncHolder& operator=(const SyncHolder&) = default;
		SyncHolder& operator=(SyncHolder&&) = default;

	private:
		template<SyncPtr TPtr> friend struct AccessSynchronizerTaskAwaiter;
		friend class TaskSystem;

		TPtr& Get() { return ptr_; }
		TPtr ptr_ = nullptr;
	};

	template<SyncPtr TPtr>
	struct AccessScope
	{
		AccessScope(TPtr resource)
			: resource_(std::move(resource))
		{
			assert(resource_);
		}
		auto operator->() { return resource_; }
	private:
		TPtr resource_;
	};

	template<SyncPtr TPtr>
	struct AccessScopeCo
	{
		AccessScopeCo(TPtr resource)
			: resource_(std::move(resource))
		{
			assert(resource_);
			assert(!AccessSynchronizer::is_any_asset_locked_); //If any other asset is locked it means there is a risk of deadlock
			DEBUG_CODE(AccessSynchronizer::is_any_asset_locked_ = true;)
		}

		auto operator->() { return resource_; }

		~AccessScopeCo()
		{
			BaseTask* local_current_task = BaseTask::GetCurrentTask();
			assert(local_current_task);
			Gate* gate = local_current_task->GetGate();
			assert(gate);
			assert(gate->GetState() == ETaskState::PendingOrExecuting);
			assert(AccessSynchronizer::is_any_asset_locked_);
			DEBUG_CODE(AccessSynchronizer::is_any_asset_locked_ = false;)
				resource_->synchronizer_.Release(*local_current_task);
			const uint32 unblocked = gate->Unblock(ETaskState::PendingOrExecuting_NonBLocking);
			assert(unblocked <= 1);
			assert(gate->IsEmpty());
		}

	private:
		TPtr resource_;
	};

	template<SyncPtr TPtr>
	struct AccessSynchronizerTaskAwaiter
	{
		AccessSynchronizerTaskAwaiter(SyncHolder<TPtr> resource)
			: resource_(std::move(resource.Get()))
		{
			assert(resource_);
		}

		bool await_ready()
		{
			if (resource_->synchronizer_.IsLocked())
			{
				return false;
			}
			BaseTask* local_current_task = BaseTask::GetCurrentTask();
			assert(local_current_task);
			Gate* gate = local_current_task->GetGate();
			assert(gate->IsEmpty());
			const ETaskState prev_state = gate->ResetStateOnEmpty(ETaskState::PendingOrExecuting);
			const bool sync_with_current = resource_->synchronizer_.SyncIfAvailible(*local_current_task);
			assert(prev_state == ETaskState::PendingOrExecuting_NonBLocking || prev_state == ETaskState::PendingOrExecuting);
			if (!sync_with_current && (prev_state == ETaskState::PendingOrExecuting_NonBLocking))
			{
				gate->Unblock(ETaskState::PendingOrExecuting_NonBLocking);
			}
			return sync_with_current;
		}
		void await_suspend(std::coroutine_handle<> handle)
		{
			assert(handle);
			TaskSystem::InitializeResumeTaskOn([handle]() {handle.resume(); }, resource_);
		}
		auto await_resume()
		{
			return AccessScopeCo<TPtr>{std::move(resource_)};
		}
	private:
		TPtr resource_;
	};
}