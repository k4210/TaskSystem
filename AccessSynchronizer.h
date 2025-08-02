#pragma once

#include "BaseTask.h"
#include <concepts>
#include <coroutine>
namespace ts
{
	struct SyncResult
	{
		TRefCountPoolPtr<BaseTask> task_;
		uint8 tag_ = 0;
	};

	struct SyncMultiResult
	{
		Index head_ = kInvalidIndex; // head of prerequires chain - AccessSynchronizer::CollectionNode
		uint32 len = 0; // num of elements in chain

		static void HandleOnTask(SyncMultiResult sync_result, BaseTask& task);
	};

	struct AccessSynchronizer
	{
		struct CollectionNode
		{
			static Index Acquire();
			static void Release(Index index);
			static std::span<CollectionNode> GetPoolSpan();
			static void ReleaseChain(Index head);

			TRefCountPoolPtr<BaseTask> task_;
			Index next_ = kInvalidIndex;
			uint8 task_tag_ = 0;

			void OnReturnToPool()
			{
				task_ = nullptr;
				task_tag_ = 0;
			}
			
		};

		struct State
		{
			uint16 tag_ = 0;
			Index last_task_ = kInvalidIndex;
			Index shared_collection_head_ = kInvalidIndex;
			uint16 shared_collection_size_ = 0;
			uint16 released_shared_tasks_ = 0;
			uint8 shared_collection_tag_ = 0;
			uint8 last_task_tag_ = 0;
		};

		//Returns head of prerequires chain - CollectionNode
		SyncMultiResult SyncExclusive(BaseTask& task, const uint8 task_tag)
		{
			task.AddRef();

			const Index task_index = GetPoolIndex(task);
			State prev_state = state_.load(std::memory_order_relaxed);
			State new_state;

			Index allocated_node = kInvalidIndex;
			Index result = kInvalidIndex;
			uint32 result_size = 0;
			do
			{
				new_state = prev_state;
				new_state.last_task_ = task_index;
				new_state.last_task_tag_ = task_tag;

				if (prev_state.shared_collection_size_) // return shared collection, then clear it
				{
					assert(prev_state.shared_collection_head_ != kInvalidIndex);
					result = prev_state.shared_collection_head_;
					result_size = prev_state.shared_collection_size_;

					new_state.shared_collection_size_ = 0;
					new_state.shared_collection_tag_ = 0;
					new_state.shared_collection_head_ = kInvalidIndex;
					new_state.released_shared_tasks_ = 0;
					new_state.tag_ = prev_state.tag_ + 1;
				}
				else if (prev_state.last_task_ == kInvalidIndex) // return empty collection
				{
					result = kInvalidIndex;
					result_size = 0;	
				}
				else // return previous exclusive_task
				{
					if (allocated_node == kInvalidIndex)
					{
						allocated_node = CollectionNode::Acquire();
					}
					CollectionNode& node = FromPoolIndex<CollectionNode>(allocated_node);
					assert(node.next_ == kInvalidIndex);
					node.task_.ResetNoRelease();
					node.task_ = TRefCountPoolPtr<BaseTask>(prev_state.last_task_, false);
					node.task_tag_ = prev_state.last_task_tag_;
					result = allocated_node;
					result_size = 1;
				}
			} while (!state_.compare_exchange_weak(prev_state, new_state,
				std::memory_order_release,
				std::memory_order_relaxed));

			if ((allocated_node != kInvalidIndex) && (result != allocated_node))
			{
				FromPoolIndex<CollectionNode>(allocated_node).task_.ResetNoRelease();
				CollectionNode::Release(allocated_node);
			}

			// release previous (replaced) exclusive task, that was not returned
			if (prev_state.shared_collection_size_ && (prev_state.last_task_tag_ != kInvalidIndex))
			{
				assert(prev_state.last_task_tag_ != new_state.last_task_tag_);
				FromPoolIndex<BaseTask>(prev_state.last_task_).Release();
			}

			return SyncMultiResult{ result, result_size };
		}

		bool SyncExclusiveIfAvailible(BaseTask& task, const uint8 task_tag)
		{
			State prev_state = state_.load(std::memory_order_relaxed);
			State new_state;
			do
			{
				if (prev_state.shared_collection_size_ || (prev_state.last_task_tag_ != kInvalidIndex))
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
		SyncResult SyncShared(BaseTask& task, const uint8 task_tag)
		{
			State prev_state = state_.load(std::memory_order_relaxed);
			State new_state;

			Index allocated_node = CollectionNode::Acquire();
			CollectionNode& node = FromPoolIndex<CollectionNode>(allocated_node);
			node.task_ = task;
			node.task_tag_ = task_tag;

			do
			{
				new_state = prev_state;
				new_state.shared_collection_head_ = allocated_node;
				new_state.shared_collection_size_ = prev_state.shared_collection_size_ + 1;
				new_state.shared_collection_tag_ = prev_state.shared_collection_tag_ + 1;

				node.next_ = prev_state.shared_collection_head_;

			} while (!state_.compare_exchange_weak(prev_state, new_state,
				std::memory_order_release,
				std::memory_order_relaxed));

			return SyncResult{
				TRefCountPoolPtr<BaseTask>(prev_state.last_task_),
				prev_state.last_task_tag_ };
		}

		//TODO: return symc tag
		bool SyncSharedIfAvailible(BaseTask& task, const uint8 task_tag)
		{
			State prev_state = state_.load(std::memory_order_relaxed);
			State new_state;
			Index allocated_node = kInvalidIndex;

			do
			{
				if (prev_state.last_task_tag_ != kInvalidIndex)
				{
					if (allocated_node != kInvalidIndex)
					{
						CollectionNode::Release(allocated_node);
					}
					return false;
				}

				if (allocated_node == kInvalidIndex)
				{
					allocated_node = CollectionNode::Acquire();
					CollectionNode& node = FromPoolIndex<CollectionNode>(allocated_node);
					node.task_ = TRefCountPoolPtr<BaseTask>(task);
					node.task_tag_ =  task_tag;
				}

				new_state = prev_state;
				new_state.shared_collection_head_ = allocated_node;
				new_state.shared_collection_size_ = prev_state.shared_collection_size_ + 1;
				new_state.shared_collection_tag_ = prev_state.shared_collection_tag_ + 1;

				FromPoolIndex<CollectionNode>(allocated_node).next_ = prev_state.shared_collection_head_;

			} while (!state_.compare_exchange_weak(prev_state, new_state,
				std::memory_order_release,
				std::memory_order_relaxed));

			return true;
		}

		void ReleaseExclusive(BaseTask& task)
		{
			const Index expexted = GetPoolIndex(task);
			State prev_state = state_.load(std::memory_order_relaxed);
			State new_state;
			do
			{
				if (prev_state.last_task_ != expexted)
				{
					return;
				}
				new_state = prev_state;
				new_state.last_task_ = kInvalidIndex;
				new_state.last_task_tag_ = 0;
			} while (!state_.compare_exchange_weak(prev_state, State{},
				std::memory_order_release,
				std::memory_order_relaxed));

			assert(task.GetRefCount() > 1);
			task.Release();
		}

		void ReleaseShared([[maybe_unused]] BaseTask& task, const uint16 sync_tag)
		{
			State prev_state = state_.load(std::memory_order_relaxed);
			State new_state;

			Index node_chain_to_release = kInvalidIndex;
			do
			{
				if (prev_state.tag_ != sync_tag)
				{
					return; //already released
				}
				new_state = prev_state;
				new_state.released_shared_tasks_ = prev_state.released_shared_tasks_ + 1;

				if (new_state.released_shared_tasks_ == new_state.shared_collection_size_)
				{
					node_chain_to_release = prev_state.shared_collection_head_;

					new_state.shared_collection_size_ = 0;
					new_state.shared_collection_tag_ = 0;
					new_state.shared_collection_head_ = kInvalidIndex;
					new_state.released_shared_tasks_ = 0;
					new_state.tag_ = prev_state.tag_ + 1;
				}
				else
				{
					node_chain_to_release = kInvalidIndex;
				}

			} while (!state_.compare_exchange_weak(prev_state, State{},
				std::memory_order_release,
				std::memory_order_relaxed));

			if (node_chain_to_release != kInvalidIndex)
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
		template<SyncPtr TPtr> friend struct AccessSynchronizerExclusiveTaskAwaiter;
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
			resource_->synchronizer_.ReleaseExclusive(*local_current_task);
			constexpr bool bump_tag = true;
			const uint32 unblocked = gate->Unblock(ETaskState::PendingOrExecuting, nullptr, bump_tag);
			assert(unblocked <= 1);
			assert(gate->IsEmpty());
		}

	private:
		TPtr resource_;
	};

	template<SyncPtr TPtr>
	struct AccessSynchronizerExclusiveTaskAwaiter
	{
		AccessSynchronizerExclusiveTaskAwaiter(SyncHolder<TPtr> resource)
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
			const SyncMultiResult result = resource_->synchronizer_.SyncExclusive(*task, task->GetTag());
			SyncMultiResult::HandleOnTask(result, *task);
		}
		auto await_resume()
		{
			return AccessScopeCo<TPtr>{std::move(resource_)};
		}
	private:
		TPtr resource_;
	};
}