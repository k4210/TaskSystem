#pragma once

#include "BaseTask.h"
#include <concepts>
#include <coroutine>
#include <utility>

namespace ts
{
	struct AccessSynchronizer
	{
		struct CollectionNode;

		using CollectionTag = BaseTag<CollectionNode, uint8>;
		using SynchroniserTag = BaseTag<AccessSynchronizer, uint16>;
		using CollectionIndex = BaseIndex<CollectionNode>;
		using TaskIndex = BaseTask::IndexType;

		struct CollectionNode
		{
			static CollectionIndex Acquire();
			static void Release(CollectionIndex index);
			static std::span<CollectionNode> GetPoolSpan();
			static void ReleaseChain(CollectionIndex head);

			TRefCountPoolPtr<BaseTask> task_;
			CollectionIndex next_;
			GateTag task_tag_;

			void OnReturnToPool()
			{
				task_ = nullptr;
				task_tag_.Reset();
			}

			CollectionIndex& NextRef()
			{
				return next_;
			}
		};

		struct State
		{
			SynchroniserTag tag_;
			TaskIndex last_task_;
			CollectionIndex shared_collection_head_;
			uint16 shared_collection_size_ = 0;
			uint16 released_shared_tasks_ = 0;
			CollectionTag shared_collection_tag_;
			GateTag last_task_tag_;
		};

		struct SyncResult
		{
			TRefCountPoolPtr<BaseTask> task_;
			GateTag task_tag_;
			SynchroniserTag synchroniser_tag_;
		};

		struct SyncMultiResult
		{
			CollectionIndex head_; // head of prerequires chain - AccessSynchronizer::CollectionNode
			uint32 len = 0; // num of elements in chain

			static void HandleOnTask(SyncMultiResult sync_result, BaseTask& task);
		};

		//Returns head of prerequires chain - CollectionNode
		SyncMultiResult SyncExclusive(BaseTask& task, const GateTag task_tag)
		{
			task.AddRef();

			const TaskIndex task_index{GetPoolIndex(task)};
			State prev_state = state_.load(std::memory_order_relaxed);
			State new_state;

			CollectionIndex allocated_node;
			CollectionIndex result;
			uint32 result_size = 0;
			do
			{
				new_state = prev_state;
				new_state.last_task_ = task_index;
				new_state.last_task_tag_ = task_tag;

				if (prev_state.shared_collection_size_) // return shared collection, then clear it
				{
					assert(prev_state.shared_collection_head_.IsValid());
					result = prev_state.shared_collection_head_;
					result_size = prev_state.shared_collection_size_;

					new_state.shared_collection_size_ = 0;
					new_state.shared_collection_tag_.Reset();
					new_state.shared_collection_head_.Reset();
					new_state.released_shared_tasks_ = 0;
					new_state.tag_ = prev_state.tag_.Next();
				}
				else if (!prev_state.last_task_.IsValid()) // return empty collection
				{
					result.Reset();
					result_size = 0;	
				}
				else // return previous exclusive_task
				{
					if (!allocated_node.IsValid())
					{
						allocated_node = CollectionNode::Acquire();
					}
					CollectionNode& node = FromPoolIndex<CollectionNode>(allocated_node);
					assert(!node.NextRef().IsValid());
					node.task_.ResetNoRelease();
					node.task_ = TRefCountPoolPtr<BaseTask>(prev_state.last_task_, false);
					node.task_tag_ = prev_state.last_task_tag_;
					result = allocated_node;
					result_size = 1;
				}
			} while (!state_.compare_exchange_weak(prev_state, new_state,
				std::memory_order_release,
				std::memory_order_relaxed));

			if (allocated_node.IsValid() && (result != allocated_node))
			{
				FromPoolIndex<CollectionNode>(allocated_node).task_.ResetNoRelease();
				CollectionNode::Release(allocated_node);
			}

			// release previous (replaced) exclusive task, that was not returned
			if (prev_state.shared_collection_size_ && prev_state.last_task_.IsValid())
			{
				assert(prev_state.last_task_ != new_state.last_task_);
				FromPoolIndex<BaseTask>(prev_state.last_task_).Release();
			}

			return SyncMultiResult{ result, result_size };
		}

		bool SyncExclusiveIfAvailible(BaseTask& task, const GateTag task_tag)
		{
			State prev_state = state_.load(std::memory_order_relaxed);
			State new_state;
			do
			{
				if (prev_state.shared_collection_size_ || prev_state.last_task_.IsValid())
				{
					return false;
				}
				new_state = prev_state;
				new_state.last_task_ = GetPoolIndex(task);;
				new_state.last_task_tag_ = task_tag;
			} while (!state_.compare_exchange_weak(prev_state, new_state,
				std::memory_order_release,
				std::memory_order_relaxed));

			task.AddRef();

			return true;
		}

		//TODO: return symc tag
		SyncResult SyncShared(BaseTask& task, const GateTag task_tag)
		{
			State prev_state = state_.load(std::memory_order_relaxed);
			State new_state;

			const CollectionIndex allocated_node = CollectionNode::Acquire();
			CollectionNode& node = FromPoolIndex<CollectionNode>(allocated_node);
			node.task_ = task;
			node.task_tag_ = task_tag;

			do
			{
				new_state = prev_state;
				new_state.shared_collection_head_ = allocated_node;
				new_state.shared_collection_size_ = prev_state.shared_collection_size_ + 1;
				assert(new_state.shared_collection_size_);
				new_state.shared_collection_tag_ = prev_state.shared_collection_tag_.Next();
				assert(new_state.released_shared_tasks_ < new_state.shared_collection_size_);

				node.NextRef() = prev_state.shared_collection_head_;

			} while (!state_.compare_exchange_weak(prev_state, new_state,
				std::memory_order_release,
				std::memory_order_relaxed));

			return SyncResult{
				TRefCountPoolPtr<BaseTask>(prev_state.last_task_),
				prev_state.last_task_tag_,
				new_state.tag_};
		}

		// return sync tag, it synced
		std::optional<SynchroniserTag> SyncSharedIfAvailible(BaseTask& task, const GateTag task_tag)
		{
			State prev_state = state_.load(std::memory_order_relaxed);
			State new_state;
			CollectionIndex allocated_node;

			do
			{
				if (prev_state.last_task_.IsValid())
				{
					if (allocated_node.IsValid())
					{
						CollectionNode::Release(allocated_node);
					}
					return {};
				}

				if (!allocated_node.IsValid())
				{
					allocated_node = CollectionNode::Acquire();
					CollectionNode& node = FromPoolIndex(allocated_node);
					node.task_ = TRefCountPoolPtr<BaseTask>(task);
					node.task_tag_ = task_tag;
				}

				new_state = prev_state;
				new_state.shared_collection_head_ = allocated_node;
				new_state.shared_collection_size_ = prev_state.shared_collection_size_ + 1;
				assert(new_state.shared_collection_size_);
				new_state.shared_collection_tag_ = prev_state.shared_collection_tag_.Next();
				assert(new_state.released_shared_tasks_ < new_state.shared_collection_size_);

				FromPoolIndex(allocated_node).NextRef() = prev_state.shared_collection_head_;

			} while (!state_.compare_exchange_weak(prev_state, new_state,
				std::memory_order_release,
				std::memory_order_relaxed));

			return new_state.tag_;
		}

		void ReleaseExclusive(BaseTask& task)
		{
			const TaskIndex expexted{GetPoolIndex(task)};
			State prev_state = state_.load(std::memory_order_relaxed);
			State new_state;
			do
			{
				if (prev_state.last_task_ != expexted)
				{
					return;
				}
				new_state = prev_state;
				new_state.last_task_.Reset();
				new_state.last_task_tag_.Reset();
			} while (!state_.compare_exchange_weak(prev_state, new_state,
				std::memory_order_release,
				std::memory_order_relaxed));

			assert(task.GetRefCount() > 1);
			task.Release();
		}

		void ReleaseShared([[maybe_unused]] BaseTask& task, const SynchroniserTag sync_tag)
		{
			State prev_state = state_.load(std::memory_order_relaxed);
			State new_state;

			CollectionIndex node_chain_to_release;
			do
			{
				if (prev_state.tag_ != sync_tag)
				{
					return; //already released
				}
				new_state = prev_state;
				assert(new_state.released_shared_tasks_ < new_state.shared_collection_size_);
				new_state.released_shared_tasks_ = prev_state.released_shared_tasks_ + 1;

				if (new_state.released_shared_tasks_ == new_state.shared_collection_size_)
				{
					node_chain_to_release = prev_state.shared_collection_head_;

					new_state.shared_collection_size_ = 0;
					new_state.shared_collection_tag_.Reset();
					new_state.shared_collection_head_.Reset();
					new_state.released_shared_tasks_ = 0;
					new_state.tag_ = prev_state.tag_.Next();
				}
				else
				{
					node_chain_to_release.Reset();
				}

			} while (!state_.compare_exchange_weak(prev_state, new_state,
				std::memory_order_release,
				std::memory_order_relaxed));

			if (node_chain_to_release.IsValid())
			{
				CollectionNode::ReleaseChain(node_chain_to_release);
			}
		}

		DEBUG_CODE(thread_local static bool is_any_asset_locked_;)
	private:
		std::atomic<State> state_;
	};

/*
	struct AccessSynchronizer
	{
		struct State
		{
			Index last_task_ = kInvalidIndex;
			uint8 tag_ = 0;
		};

		//returns prerequire, to insert
		SyncResult Sync(BaseTask& task, const uint8 tag)
		{
			task.AddRef();
			const State prev_state = state_.exchange(State{GetPoolIndex(task), tag});
			DEBUG_CODE(BaseTask* prev = (prev_state.last_task_ != kInvalidIndex)
				? &FromPoolIndex<BaseTask>(prev_state.last_task_) 
				: nullptr;)
			assert(prev != &task);
			assert(!prev || (prev->GetRefCount() > 1) || prev->GetGate()->IsEmpty());
			return SyncResult{
				TRefCountPoolPtr<BaseTask>(prev_state.last_task_, false),
				prev_state.tag_};
		}

		bool SyncIfAvailible(BaseTask& task, const uint8 tag)
		{
			task.AddRef();
			State expexted;
			const bool replaced = state_.compare_exchange_strong(expexted, State{GetPoolIndex(task), tag});
			if (!replaced)
			{
				task.Release();
			}
			return replaced;
		}

		void Release(BaseTask& task)
		{
			const Index expexted = GetPoolIndex(task);
			State prev_state = state_.load(std::memory_order_relaxed);
			do
			{
				if (prev_state.last_task_ != expexted)
				{
					return;
				}
			} while (!state_.compare_exchange_weak(prev_state, State{},
				std::memory_order_release,
				std::memory_order_relaxed));

			assert(task.GetRefCount() > 1);
			task.Release();
		}

		bool IsLocked() const
		{
			return state_.load(std::memory_order_relaxed).last_task_ != kInvalidIndex;
		}

		DEBUG_CODE(thread_local static bool is_any_asset_locked_;)
	private:
		std::atomic<State> state_;
	};
*/
	template<typename T>
	concept SyncT = requires(T inst)
	{
		{ inst.synchronizer_ } -> std::same_as<AccessSynchronizer&>;
	};

	template<SyncT TValue>
	struct SharedSyncHolder
	{
		SharedSyncHolder(TValue* ptr) : ptr_(ptr) {}
		SharedSyncHolder(const SharedSyncHolder&) = default;
		SharedSyncHolder(SharedSyncHolder&&) = default;
		SharedSyncHolder& operator=(const SharedSyncHolder&) = default;
		SharedSyncHolder& operator=(SharedSyncHolder&&) = default;

	private:
		template<SyncT TValue> friend struct AccessSynchronizerSharedTaskAwaiter;
		template<SyncT TValue> friend struct SharedSyncHolder;
		friend class TaskSystem;

		TValue* Get() { return ptr_; }
		TValue* ptr_ = nullptr;
	};

	template<SyncT TValue>
	struct SyncHolder
	{
		SyncHolder(TValue* ptr) : ptr_(ptr) {}
		SyncHolder(const SyncHolder&) = default;
		SyncHolder(SyncHolder&&) = default;
		SyncHolder& operator=(const SyncHolder&) = default;
		SyncHolder& operator=(SyncHolder&&) = default;

		SharedSyncHolder<TValue> Shared()
		{
			return SharedSyncHolder<TValue>(Get());
		}

	private:
		template<SyncT TValue> friend struct AccessSynchronizerExclusiveTaskAwaiter;
		friend class TaskSystem;

		TValue* Get() { return ptr_; }
		TValue* ptr_ = nullptr;
	};

	template<SyncT TValue>
	struct AccessScope
	{
		AccessScope(TValue* resource)
			: resource_(std::move(resource))
		{
			assert(resource_);
		}
		auto operator->() { return resource_; }
	private:
		TValue* resource_;
	};

	template<SyncT TValue>
	struct AccessScopeCo
	{
		AccessScopeCo(TValue* resource)
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
			resource_->synchronizer_.ReleaseExclusive(*local_current_task);
			constexpr bool bump_tag = true;
			//const uint32 unblocked = 
				gate->Unblock(ETaskState::PendingOrExecuting, nullptr, bump_tag);
			//assert(unblocked <= 1);
			assert(gate->IsEmpty());
		}

	private:
		TValue* resource_;
	};

	template<SyncT TValue>
	struct AccessSynchronizerExclusiveTaskAwaiter
	{
		AccessSynchronizerExclusiveTaskAwaiter(SyncHolder<TValue> resource)
			: resource_(std::move(resource.Get()))
		{
			assert(resource_);
		}

		bool await_ready()
		{
			BaseTask* local_current_task = BaseTask::GetCurrentTask();
			assert(local_current_task);
			Gate* gate = local_current_task->GetGate();
			assert(gate->IsEmpty());
			assert(gate->GetState() == ETaskState::PendingOrExecuting);
			// Seems redundant:
			//constexpr bool bump_tag = true;
			//const ETaskState prev_state = gate->ResetStateOnEmpty(ETaskState::PendingOrExecuting, bump_tag);
			//assert(prev_state == ETaskState::PendingOrExecuting);
			return resource_->synchronizer_.SyncExclusiveIfAvailible(*local_current_task, local_current_task->GetTag());
		}
		void await_suspend(std::coroutine_handle<> handle)
		{
			TRefCountPtr<BaseTask> task;
#if TASK_RETRIGGER
			BaseTask* local_current_task = BaseTask::GetCurrentTask();
			if (local_current_task && (kInvalidIndex != t_worker_thread_idx) 
				&& (local_current_task->GetRefCount() == 1)) // So noone can insert dependency during this scope
			{
				local_current_task->SetRetrigger();
				local_current_task->GetGate()->Unblock(ETaskState::PendingOrExecuting);
				assert(local_current_task->GetRefCount() == 1); // If dependency is added here, then there may be a deadlock
				task = local_current_task;
			}
			else
#endif
			{
				assert(handle);
				task = TaskSystem::CreateTask([handle](BaseTask&){ handle.resume(); });
			}

			assert(resource_);
			const AccessSynchronizer::SyncMultiResult result = resource_->synchronizer_.SyncExclusive(*task, task->GetTag());
			AccessSynchronizer::SyncMultiResult::HandleOnTask(result, *task);
		}
		auto await_resume()
		{
			return AccessScopeCo<TValue>{std::move(resource_)};
		}
	private:
		TValue* resource_;
	};

	template<SyncT TValue>
	struct SharedAccessScopeCo
	{
		SharedAccessScopeCo(TValue* resource, AccessSynchronizer::SynchroniserTag synchroniser_tag)
			: resource_(std::move(resource)), synchroniser_tag_(synchroniser_tag)
		{
			assert(resource_);
		}

		auto operator->() { return &std::as_const(*resource_); }

		~SharedAccessScopeCo()
		{
			BaseTask* local_current_task = BaseTask::GetCurrentTask();
			assert(local_current_task);
			Gate* gate = local_current_task->GetGate();
			assert(gate);
			assert(gate->GetState() == ETaskState::PendingOrExecuting);
			resource_->synchronizer_.ReleaseShared(*local_current_task, synchroniser_tag_);
			constexpr bool bump_tag = true;
			const uint32 unblocked = gate->Unblock(ETaskState::PendingOrExecuting, nullptr, bump_tag);
			assert(unblocked <= 1);
			assert(gate->IsEmpty());
		}

	private:
		TValue* resource_;
		AccessSynchronizer::SynchroniserTag synchroniser_tag_;
	};

	template<SyncT TValue>
	struct AccessSynchronizerSharedTaskAwaiter
	{
		AccessSynchronizerSharedTaskAwaiter(SharedSyncHolder<TValue> resource)
			: resource_(std::move(resource.Get()))
		{
			assert(resource_);
		}

		bool await_ready()
		{
			BaseTask* local_current_task = BaseTask::GetCurrentTask();
			assert(local_current_task);
			Gate* gate = local_current_task->GetGate();
			assert(gate->IsEmpty());
			assert(gate->GetState() == ETaskState::PendingOrExecuting);
			// Seems redundant:
			//constexpr bool bump_tag = true;
			//const ETaskState prev_state = gate->ResetStateOnEmpty(ETaskState::PendingOrExecuting, bump_tag);
			//assert(prev_state == ETaskState::PendingOrExecuting);
			assert(!synrchoniser_tag_);
			synrchoniser_tag_ = resource_->synchronizer_.SyncSharedIfAvailible(*local_current_task, local_current_task->GetTag());
			return synrchoniser_tag_.has_value();
		}
		void await_suspend(std::coroutine_handle<> handle)
		{
			TRefCountPtr<BaseTask> task;
#if TASK_RETRIGGER
			BaseTask* local_current_task = BaseTask::GetCurrentTask();
			if (local_current_task && (kInvalidIndex != t_worker_thread_idx) 
				&& (local_current_task->GetRefCount() == 1)) // So noone can insert dependency during this scope
			{
				local_current_task->SetRetrigger();
				local_current_task->GetGate()->Unblock(ETaskState::PendingOrExecuting);
				assert(local_current_task->GetRefCount() == 1); // If dependency is added here, then there may be a deadlock
				task = local_current_task;
			}
			else
#endif
			{
				assert(handle);
				task = TaskSystem::CreateTask([handle](BaseTask&){ handle.resume(); });
			}

			assert(resource_);
			const AccessSynchronizer::SyncResult result = resource_->synchronizer_.SyncShared(*task, task->GetTag());
			assert(!synrchoniser_tag_);
			synrchoniser_tag_ = result.synchroniser_tag_;

			TRefCountPtr<BaseTask> prev_task_to_sync = result.task_.ToRefCountPtr();
			Gate* const to_sync = prev_task_to_sync ? prev_task_to_sync->GetGate() : nullptr;
			DEBUG_CODE(const ETaskState prev_state = to_sync ? to_sync->GetState() : ETaskState::Nonexistent_Pooled;)
			assert(!to_sync || (prev_state != ETaskState::Nonexistent_Pooled));
			Gate* pre_req[] = { to_sync };
			uint8 pre_req_tags[] = { result.task_tag_.RawValue() };
			TaskSystem::HandlePrerequires(*task, pre_req, pre_req_tags);
		}
		auto await_resume()
		{
			return SharedAccessScopeCo<TValue>{std::move(resource_), *synrchoniser_tag_};
		}
	private:
		TValue* resource_;
		std::optional<AccessSynchronizer::SynchroniserTag> synrchoniser_tag_;
	};
}