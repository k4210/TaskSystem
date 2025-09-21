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
		using SynchroniserTag = BaseTag<AccessSynchronizer, uint8>;
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
			TaskIndex last_task_;
			CollectionIndex shared_collection_head_;
			uint16 shared_collection_size_ = 0;//TODO uint8
			uint16 released_shared_tasks_ = 0;
			//CollectionTag shared_collection_tag_; // Not neeeded, shared_collection_size_ and tag_ should be unique enough
			GateTag last_task_tag_;
			SynchroniserTag tag_; //bumped when shared collection is reset

			void Validate()
			{
				assert(!last_task_.IsValid() || FromPoolIndex(last_task_).GetRefCount() > 0);
			}
		};

		struct SyncMultiResult
		{
			// Single
			TRefCountPoolPtr<BaseTask> task_;
			GateTag task_tag_;

			//many
			CollectionIndex head_; // head of prerequires chain - AccessSynchronizer::CollectionNode
			
			//Common
			uint32 len_ = 0; // num of elements in chain
			SynchroniserTag synchroniser_tag_;

			void ResetNoRelease()
			{
				task_.ResetNoRelease();
				task_tag_.Reset();
				head_.Reset();
				len_ = 0;
				synchroniser_tag_.Reset();
			}

			void SetSingle(TRefCountPoolPtr<BaseTask> in_task, GateTag in_task_tag)
			{
				assert(!head_.IsValid());
				assert(in_task.GetRefCount() > 0);
				task_ = std::move(in_task);
				task_tag_ = in_task_tag;
				len_ =	1;
			}

			void SetMulti(CollectionIndex in_head, uint32 in_len)
			{
				assert(!task_);
				head_ = in_head;
				len_ = in_len;
			}

			void SetSynchroniserTag(SynchroniserTag in_tag)
			{
				synchroniser_tag_ = in_tag;
			}

			bool IsSingle() const
			{
				assert(!task_.IsValid() || !head_.IsValid());
				assert(!task_.IsValid() || (len_ == 1));
				return task_.IsValid();
			}

			static void HandleOnTask(SyncMultiResult sync_result, BaseTask& task);
		};

		//Returns head of prerequires chain - CollectionNode
		SyncMultiResult SyncExclusive(BaseTask& task, const GateTag task_tag)
		{
			assert(task.GetRefCount() > 0);
			task.AddRef();

			const TaskIndex task_index{GetPoolIndex(task)};
			State prev_state = state_.load(std::memory_order_relaxed);
			prev_state.Validate();
			State new_state;

			SyncMultiResult result;
			do
			{
				result.ResetNoRelease();

				new_state = prev_state;
				new_state.last_task_ = task_index;
				new_state.last_task_tag_ = task_tag;

				if (prev_state.shared_collection_size_) // return shared collection, then clear it
				{
					assert(prev_state.shared_collection_head_.IsValid());
					result.SetMulti(prev_state.shared_collection_head_, prev_state.shared_collection_size_);

					new_state.shared_collection_size_ = 0;
					new_state.shared_collection_head_.Reset();
					new_state.released_shared_tasks_ = 0;
					new_state.tag_ = prev_state.tag_.Next();
				}
				else if (prev_state.last_task_.IsValid()) // return previous exclusive_task
				{
					result.SetSingle(
						TRefCountPoolPtr<BaseTask>(prev_state.last_task_, false),
						prev_state.last_task_tag_);
				}
			} while (!state_.compare_exchange_weak(prev_state, new_state,
				std::memory_order_release,
				std::memory_order_relaxed));

			// release previous (replaced) exclusive task, that was not returned
			if (prev_state.shared_collection_size_ && prev_state.last_task_.IsValid())
			{
				assert(prev_state.last_task_ != new_state.last_task_);
				FromPoolIndex(prev_state.last_task_).Release();
			}

			new_state.Validate();
			result.SetSynchroniserTag(new_state.tag_);
			return result;
		}

		bool SyncExclusiveIfAvailible(BaseTask& task, const GateTag task_tag)
		{
			assert(task.GetRefCount() > 0);
			task.AddRef();
			
			const TaskIndex task_index{GetPoolIndex(task)};
			State prev_state = state_.load(std::memory_order_relaxed);
			prev_state.Validate();
			State new_state;
			do
			{
				if (prev_state.shared_collection_size_ || prev_state.last_task_.IsValid())
				{
					task.Release();
					prev_state.Validate();
					return false;
				}
				new_state = prev_state;
				new_state.last_task_ = task_index;
				new_state.last_task_tag_ = task_tag;
			} while (!state_.compare_exchange_weak(prev_state, new_state,
				std::memory_order_release,
				std::memory_order_relaxed));

			new_state.Validate();

			return true;
		}

		SyncMultiResult SyncShared(BaseTask& task, const GateTag task_tag)
		{
			assert(task.GetRefCount() > 0);
			State prev_state = state_.load(std::memory_order_relaxed);
			prev_state.Validate();
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
				assert(new_state.released_shared_tasks_ < new_state.shared_collection_size_);

				node.NextRef() = prev_state.shared_collection_head_;

			} while (!state_.compare_exchange_weak(prev_state, new_state,
				std::memory_order_release,
				std::memory_order_relaxed));

			new_state.Validate();

			SyncMultiResult result;
			result.SetSingle(TRefCountPoolPtr<BaseTask>(prev_state.last_task_), prev_state.last_task_tag_);
			result.SetSynchroniserTag(new_state.tag_);
			return result;
		}

		// return sync tag, it synced
		std::optional<SynchroniserTag> SyncSharedIfAvailible(BaseTask& task, const GateTag task_tag)
		{
			assert(task.GetRefCount() > 0);
			State prev_state = state_.load(std::memory_order_relaxed);
			prev_state.Validate();
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
					prev_state.Validate();
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
				assert(new_state.released_shared_tasks_ < new_state.shared_collection_size_);

				FromPoolIndex(allocated_node).NextRef() = prev_state.shared_collection_head_;

			} while (!state_.compare_exchange_weak(prev_state, new_state,
				std::memory_order_release,
				std::memory_order_relaxed));

			new_state.Validate();
			return new_state.tag_;
		}

		void ReleaseExclusive(BaseTask& task)
		{
			const TaskIndex expexted{GetPoolIndex(task)};
			State prev_state = state_.load(std::memory_order_relaxed);
			prev_state.Validate();
			State new_state;
			do
			{
				if (prev_state.last_task_ != expexted)
				{
					prev_state.Validate();
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

			new_state.Validate();
		}

		void ReleaseShared([[maybe_unused]] BaseTask& task, const SynchroniserTag sync_tag)
		{
			State prev_state = state_.load(std::memory_order_relaxed);
			prev_state.Validate();
			State new_state;

			CollectionIndex node_chain_to_release;
			do
			{
				if (prev_state.tag_ != sync_tag)
				{
					prev_state.Validate();
					return; //already released
				}
				new_state = prev_state;
				assert(new_state.released_shared_tasks_ < new_state.shared_collection_size_);
				new_state.released_shared_tasks_ = prev_state.released_shared_tasks_ + 1;

				if (new_state.released_shared_tasks_ == new_state.shared_collection_size_)
				{
					node_chain_to_release = prev_state.shared_collection_head_;

					new_state.shared_collection_size_ = 0;
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
				assert(task.GetRefCount() > 0);
			}

			new_state.Validate();
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
			Gate& gate = local_current_task->GetGate();
			assert(gate.GetState() == ETaskState::PendingOrExecuting);
			assert(AccessSynchronizer::is_any_asset_locked_);
			DEBUG_CODE(AccessSynchronizer::is_any_asset_locked_ = false;)
			resource_->synchronizer_.ReleaseExclusive(*local_current_task);
			constexpr bool bump_tag = true;
			gate.Unblock(ETaskState::PendingOrExecuting, nullptr, bump_tag);
			assert(gate.IsEmpty());
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
			Gate& gate = local_current_task->GetGate();
			assert(gate.IsEmpty());
			assert(gate.GetState() == ETaskState::PendingOrExecuting);
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
			AccessSynchronizer::SyncMultiResult result = resource_->synchronizer_.SyncExclusive(*task, task->GetTag());
			AccessSynchronizer::SyncMultiResult::HandleOnTask(std::move(result), *task);
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

		auto operator->() 
		{ 
			//return &std::as_const(*resource_); 
			return resource_;
		}

		~SharedAccessScopeCo()
		{
			BaseTask* local_current_task = BaseTask::GetCurrentTask();
			assert(local_current_task);
			Gate& gate = local_current_task->GetGate();
			assert(gate.GetState() == ETaskState::PendingOrExecuting);
			resource_->synchronizer_.ReleaseShared(*local_current_task, synchroniser_tag_);
			constexpr bool bump_tag = true;
			const uint32 unblocked = gate.Unblock(ETaskState::PendingOrExecuting, nullptr, bump_tag);
			assert(unblocked <= 1);
			assert(gate.IsEmpty());
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
			Gate& gate = local_current_task->GetGate();
			assert(gate.IsEmpty());
			assert(gate.GetState() == ETaskState::PendingOrExecuting);
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
				local_current_task->GetGate().Unblock(ETaskState::PendingOrExecuting);
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
			AccessSynchronizer::SyncMultiResult result = resource_->synchronizer_.SyncShared(*task, task->GetTag());
			assert(!synrchoniser_tag_);
			synrchoniser_tag_ = result.synchroniser_tag_;

			AccessSynchronizer::SyncMultiResult::HandleOnTask(std::move(result), *task);
		}
		auto await_resume()
		{
			assert(synrchoniser_tag_);
			return SharedAccessScopeCo<TValue>{std::move(resource_), *synrchoniser_tag_};
		}
	private:
		TValue* resource_;
		std::optional<AccessSynchronizer::SynchroniserTag> synrchoniser_tag_;
	};
}