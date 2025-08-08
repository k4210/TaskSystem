#pragma once

#include <iterator>
#include <array>
#include <algorithm>
#include <random>
#include "LockFree.h"

namespace ts
{
	extern thread_local uint16 t_worker_thread_idx;
}

#if DO_POOL_STATS
#define POOL_STATS(x) x
#else
#define POOL_STATS(x)
#endif

namespace ts
{
	template<typename Node>
	struct UnsafeStack
	{
		using IndexType = std::remove_cvref_t<decltype(Node{}.NextRef())>;

		void Push(Node& node)
		{
			const IndexType idx = GetPoolIndex(node);
			node.NextRef() = head_;
			head_ = idx;
			size_++;
		}

		void PushChain(Node& new_head, Node& chain_tail, uint16 len)
		{
			const IndexType idx = GetPoolIndex(new_head);
			chain_tail.NextRef() = head_;
			head_ = idx;
			size_ += len;
		}

		Node* Pop()
		{
			if (!head_.IsValid())
			{
				return nullptr;
			}
			Node& node = FromPoolIndex<Node>(head_);
			auto& next_ref = node.NextRef();
			head_ = next_ref;
			next_ref.Reset();
			size_--;
			return &node;
		}

		uint16 GetSize() const { return size_; }

	private:
		IndexType head_;
		uint16 size_ = 0;
	};

	template<typename Node, std::size_t Size
#if THREAD_SMART_POOL
		, std::size_t NumThreads, std::size_t ElementsPerThread, std::size_t MaxElementsPerThread
#endif
	>
	struct Pool
	{
		using IndexType = std::remove_cvref_t<decltype(Node{}.NextRef())>;

		Pool()
		{
#if 1
            for (Index Idx = 1; Idx < Size; Idx++)
            {
                all_[Idx - 1].NextRef() = Idx;
            }
            Index first_remaining{ 0 };
#else
            std::vector<Index> initial_order;
            initial_order.reserve(Size);
            for (Index Idx = 0; Idx < Size; Idx++)
            {
                initial_order.push_back(Idx);
            }
            std::random_device rd;
            std::mt19937 g(rd());
            std::shuffle(initial_order.begin(), initial_order.end(), g);
            for (Index Idx = 0; Idx < (Size - 1); Idx++)
            {
                all_[initial_order[Idx]].NextRef() = initial_order[Idx+1];
            }

            Index first_remaining = initial_order[0];
#endif
			
			POOL_STATS(global_free_counter_= Size;)
#if THREAD_SMART_POOL
			for (int32 thread_idx = 0; thread_idx < NumThreads; thread_idx++)
			{
				UnsafeStack<Node>& stack = free_per_thread_[thread_idx];
				const Index last_element = first_remaining + ElementsPerThread - 1;
				all_[last_element].NextRef() = kInvalidIndex;
				stack.PushChain(all_[first_remaining], all_[last_element], ElementsPerThread);
				first_remaining += ElementsPerThread;
			}
			POOL_STATS(global_free_counter_ -= NumThreads * ElementsPerThread;)
			POOL_STATS(thread_free_counter_ = NumThreads * ElementsPerThread;)
#endif
			free_.Reset(IndexType{first_remaining});
		}

#if THREAD_SMART_POOL
		UnsafeStack<Node>* GetStackPerThread()
		{
			return (t_worker_thread_idx != kInvalidIndex)
				? &free_per_thread_[t_worker_thread_idx]
				: nullptr;
		}
#endif

		Node& Acquire()
		{
#if THREAD_SMART_POOL
			UnsafeStack<Node>* thread_stack = GetStackPerThread();
			const bool use_thread_stack = (thread_stack && thread_stack->GetSize());
			Node* ptr = use_thread_stack
				? thread_stack->Pop()
				: free_.Pop();
#else
			Node* ptr = free_.Pop();
#endif
#if DO_POOL_STATS
			if (ptr)
			{
				const uint32 loc_counter = ++used_counter_;
				max_used = std::max(loc_counter, max_used);
#if THREAD_SMART_POOL
				if (!use_thread_stack)
				{
					assert(global_free_counter_ > 0);
					global_free_counter_--;
				}
				else
				{
					assert(thread_free_counter_ > 0);
					thread_free_counter_--;
				}
#endif
			}
#endif
			assert(ptr);
			return *ptr;
		}

		void Return(Node& node)
		{
			if constexpr (requires { node.OnReturnToPool(); })
			{
				node.OnReturnToPool();
			}
			POOL_STATS(assert(used_counter_ > 0);)
			POOL_STATS(used_counter_--;)
#if THREAD_SMART_POOL
			UnsafeStack<Node>* thread_stack = GetStackPerThread();
			if (thread_stack && (thread_stack->GetSize() < MaxElementsPerThread))
			{
				thread_stack->Push(node);
				POOL_STATS(thread_free_counter_++;)
				return;
			}
			POOL_STATS(global_free_counter_++;)
#endif
			free_.Push(node);
		}

		void ReturnChain(Node& new_head, Node& chain_tail, [[maybe_unused]] uint16 chain_len)
		{
			assert(chain_tail.NextRef() == kInvalidIndex);
			POOL_STATS(used_counter_ -= chain_len;)
			if constexpr (requires { new_head.OnReturnToPool(); })
			{
				for (IndexType iter{ GetPoolIndex(new_head) };
					iter != kInvalidIndex;
					iter = FromPoolIndex<Node>(iter).NextRef())
				{
					FromPoolIndex<Node>(iter).OnReturnToPool();
				}
			}
#if THREAD_SMART_POOL
			UnsafeStack<Node>* thread_stack = GetStackPerThread();
			if (thread_stack && ((thread_stack->GetSize() + chain_len) <= MaxElementsPerThread))
			{
				thread_stack->PushChain(new_head, chain_tail, chain_len);
				POOL_STATS(thread_free_counter_ += chain_len;)
				return;
			}
			POOL_STATS(global_free_counter_ += chain_len;)
#endif
			free_.PushChain(new_head, chain_tail);
		}

		std::span<Node> GetPoolSpan()
		{
			return all_;
		}

		bool BelonsTo(const Node& node)
		{
			const Node* ptr = all_.data();
			return (&node >= ptr) && (std::distance(ptr, &node) < Size);
		}

#if DO_POOL_STATS
		uint32 GetMaxUsedNum() { return max_used; }
		void AssertEmpty()
		{
			assert(!used_counter_);
			assert(thread_free_counter_ + global_free_counter_ == Size);
		}
		~Pool()
		{
			AssertEmpty();
		}
#endif

	private:
		lock_free::Stack<Node> free_;
		std::array<Node, Size> all_;
#if DO_POOL_STATS
		std::atomic_uint32_t used_counter_ = 0;
		std::atomic_uint32_t global_free_counter_ = 0;
		std::atomic_uint32_t thread_free_counter_ = 0;
		uint32 max_used = 0;
#endif
#if THREAD_SMART_POOL
		std::array<UnsafeStack<Node>, NumThreads> free_per_thread_;
#endif
	};

}