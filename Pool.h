#pragma once

#include <iterator>
#include <array>
#include "LockFree.h"

template<typename Node>
LockFree::Index GetPoolIndex(const Node& node)
{
	const std::span<Node> nodes = Node::GetPoolSpan();
	const Node* first = nodes.data();
	const LockFree::Index idx = static_cast<LockFree::Index>(std::distance(first, &node));
	assert(idx < nodes.size());
	return idx;
}

template<typename Node>
Node& FromPoolIndex(const LockFree::Index index)
{
	std::span<Node> nodes = Node::GetPoolSpan();
	assert(index < nodes.size());
	return nodes[index];
}

#ifdef _DEBUG
#define DO_POOL_STATS 1
#else
#define DO_POOL_STATS 0
#endif // _DEBUG

extern thread_local uint16 t_worker_thread_idx;

template<typename Node>
struct UnsafeStack
{
	void Push(Node& node)
	{
		const LockFree::Index idx = GetPoolIndex(node);
		node.next_ = head_;
		head_ = idx;
		size_++;
	}

	void PushChain(Node& new_head, Node& chain_tail, uint16 len)
	{
		const LockFree::Index idx = GetPoolIndex(new_head);
		chain_tail.next_ = head_;
		head_ = idx;
		size_ += len;
	}

	Node* Pop()
	{
		if (LockFree::kInvalidIndex == head_)
		{
			return nullptr;
		}
		Node& node = FromPoolIndex<Node>(head_);
		head_ = node.next_;
		node.next_ = LockFree::kInvalidIndex;
		size_--;
		return &node;
	}

	//return previous head
	LockFree::Index Reset(LockFree::Index new_head = LockFree::kInvalidIndex)
	{
		return std::exchange(head_, new_head);
	}

	uint16 GetSize() const { return size_; }

private:
	LockFree::Index head_ = LockFree::kInvalidIndex;
	uint16 size_ = 0;
};

#define THREAD_SMART_POOL 0

template<typename Node, std::size_t Size
#if THREAD_SMART_POOL
	, std::size_t NumThreads, std::size_t ElementsPerThread, std::size_t MaxElementsPerThread
#endif
>
struct Pool
{
	Pool()
	{
		for (LockFree::Index Idx = 1; Idx < Size; Idx++)
		{
			all_[Idx - 1].next_ = Idx;
		}
		LockFree::Index first_remaining = 0;
#if THREAD_SMART_POOL
		for (int32 thread_idx = 0; thread_idx < NumThreads; thread_idx++)
		{
			UnsafeStack<Node> stack = free_per_thread_[thread_idx];
			LockFree::Index last_element = first_remaining + ElementsPerThread - 1;
			all_[last_element].next_ = LockFree::kInvalidIndex;
			stack.PushChain(all_[first_remaining], all_[last_element], ElementsPerThread);
			first_remaining += ElementsPerThread;
		}
#endif
		free_.Reset(first_remaining);
	}

#if THREAD_SMART_POOL
	UnsafeStack<Node>* GetStackPerThread() 
	{
		return (t_worker_thread_idx != LockFree::kInvalidIndex)
			? &free_per_thread_[t_worker_thread_idx]
			: nullptr;
	}
#endif

	Node& Acquire()
	{
#if THREAD_SMART_POOL
		UnsafeStack<Node>* thread_stack = GetStackPerThread();
		Node* ptr = (thread_stack && thread_stack->GetSize())
			? thread_stack->Pop()
			: free_.Pop();
#else
		Node* ptr = free_.Pop();
#endif
#if DO_POOL_STATS
		if (ptr)
		{
			uint32 loc_counter = ++used_counter_;
			max_used = std::max(loc_counter, max_used);
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
#if DO_POOL_STATS
		--used_counter_;
#endif
#if THREAD_SMART_POOL
		UnsafeStack<Node>* thread_stack = GetStackPerThread();
		if (thread_stack && (thread_stack->GetSize() < MaxElementsPerThread))
		{
			thread_stack->Push(node);
			return;
		}
#endif
		free_.Push(node);
	}

	void ReturnChain(Node& new_head, Node& chain_tail, [[maybe_unused]]uint16 chain_len)
	{
		assert(chain_tail.next_ == LockFree::kInvalidIndex);
#if DO_POOL_STATS
		for (LockFree::Index iter = GetPoolIndex(new_head);
			iter != LockFree::kInvalidIndex;
			iter = FromPoolIndex<Node>(iter).next_)
		{
			--used_counter_;
		}
#endif
		if constexpr (requires { new_head.OnReturnToPool(); })
		{
			for (LockFree::Index iter = GetPoolIndex(new_head);
				iter != LockFree::kInvalidIndex; 
				iter = FromPoolIndex<Node>(iter).next_)
			{
				FromPoolIndex<Node>(iter).OnReturnToPool();
			}
		}
#if THREAD_SMART_POOL
		UnsafeStack<Node>* thread_stack = GetStackPerThread();
		if (thread_stack && ((thread_stack->GetSize() + chain_len) <= MaxElementsPerThread))
		{
			thread_stack->PushChain(new_head, chain_tail, chain_len);
			return;
		}
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
	uint32 GetUsedNum() { return used_counter_; }
#endif

private:
	LockFree::Stack<Node> free_;
	std::array<Node, Size> all_;
#if DO_POOL_STATS
	std::atomic_uint32_t used_counter_ = 0;
	uint32 max_used = 0;
#endif
#if THREAD_SMART_POOL
	std::array<UnsafeStack<Node>, NumThreads> free_per_thread_;	
#endif
};

