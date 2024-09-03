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

#define DO_POOL_STATS 0

template<typename Node, std::size_t Size>
struct Pool
{
	Pool()
	{
		for (LockFree::Index Idx = 1; Idx < Size; Idx++)
		{
			all_[Idx - 1].next_ = Idx;
		}
		free_.Reset(0);
	}

	Node& Acquire()
	{
		Node* ptr = free_.Pop();
#if DO_POOL_STATS
		uint32 loc_counter = ++used_counter_;
		max_used = std::max(loc_counter, max_used);
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
		free_.Push(node);
	}

	void ReturnChain(Node& new_head, Node& chain_tail)
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
		free_.PushChain(new_head, chain_tail);
	}

	std::span<Node> GetPoolSpan()
	{
		return all_;
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
};

