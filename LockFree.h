#pragma once

#include <atomic>
#include <span>
#include <limits>

#include <assert.h>
#include <array>
#include "BaseTypes.h"

namespace LockFree
{
	using Index = uint16;
	using Tag = uint16;
	constexpr Index kInvalidIndex = std::numeric_limits<Index>::max();

	template<typename Node>
	struct Stack
	{
		void Push(Node& node)
		{
			const Index idx = GetPoolIndex(node);
			State new_state{ .head = idx };
			State state = state_.load(std::memory_order_relaxed);
			do
			{
				new_state.tag = state.tag + 1;
				node.next_ = state.head;
			} while (!state_.compare_exchange_weak(state, new_state,
				std::memory_order_release,
				std::memory_order_relaxed));
		}

		void PushChain(Node& new_head, Node& chain_tail)
		{
			const Index idx = GetPoolIndex(new_head);
			State new_state{ .head = idx };
			State state = state_.load(std::memory_order_relaxed);
			do
			{
				new_state.tag = state.tag + 1;
				chain_tail.next_ = state.head;
			} while (!state_.compare_exchange_weak(state, new_state,
				std::memory_order_release,
				std::memory_order_relaxed));
		}

		Node* Pop()
		{
			State state = state_.load(std::memory_order_relaxed);
			State new_state;
			do
			{
				if (kInvalidIndex == state.head)
				{
					return nullptr;
				}
				new_state.head = FromPoolIndex<Node>(state.head).next_;
				new_state.tag = state.tag + 1;
			} while (!state_.compare_exchange_weak(state, new_state,
				std::memory_order_release,
				std::memory_order_relaxed));

			Node& node = FromPoolIndex<Node>(state.head);
			node.next_ = kInvalidIndex;
			return &node;
		}

		//return previous head
		Index Reset(Index new_head = kInvalidIndex)
		{
			State state = state_.load(std::memory_order_relaxed);
			const State new_state{ new_head, 0 };
			while (!state_.compare_exchange_weak(state, new_state,
				std::memory_order_release,
				std::memory_order_relaxed));
			return state.head;
		}

	private:
		struct State
		{
			Index head = kInvalidIndex;
			Tag tag = 0;
		};

		std::atomic<State> state_;
	};

	template<typename Node, typename Gate>
	struct Collection
	{
		struct State
		{
			Index head = kInvalidIndex;
			Gate gate = {};

			bool operator== (const State&) const = default;
		};

		Collection(Gate gate)
			: state_(State{ .gate = gate })
		{}

		State GetState() const
		{
			return state_.load(std::memory_order_relaxed);
		}

		Gate GetGateState() const
		{
			return GetState().gate;
		}

		// Returns old gate
		Gate SetFastOnEmpty(const Gate new_gate)
		{
			State new_state{ kInvalidIndex, new_gate };
			const State old_state = state_.exchange(new_state, std::memory_order_relaxed);
			assert(old_state.head == kInvalidIndex);
			return old_state.gate;
		}

		//return if the element was added
		bool Add(Node& node, const Gate open)
		{
			const Index idx = GetPoolIndex(node);
			const State new_state{ .head = idx, .gate = open };
			State state = state_.load(std::memory_order_relaxed);
			do
			{
				if (state.gate != open)
				{
					return false;
				}
				node.next_ = state.head;
			} while (!state_.compare_exchange_weak(state, new_state,
				std::memory_order_release,
				std::memory_order_relaxed));
			return true;
		}

		//Should be called once.
		template<typename Func>
		void CloseAndConsume(const Gate closed, Func& func)
		{
			const State old_state = state_.exchange(State{ .gate = closed });

			Index head = old_state.head;
			while (head != kInvalidIndex)
			{
				Node& node = FromPoolIndex<Node>(head);
				head = node.next_;
				func(node); // node.next_ should be handled here
			}
		}

	private:
		std::atomic<State> state_;
	};

	template<std::size_t Size>
	struct IndexQueue
	{
		void Push(Index val)
		{
			assert(val != kInvalidIndex);
			State state = state_.load(std::memory_order_relaxed);
			State new_state;
			do
			{
				assert(state.size_ < Size);
				new_state = State{ state.first_, state.size_ + 1, state.tag_ + 1 };
			} while (!state_.compare_exchange_weak(state, new_state,
				std::memory_order_release,
				std::memory_order_relaxed));

			const Index idx = (new_state.first_ + new_state.size_) % Size;
			assert(array_[idx].load(std::memory_order_relaxed) == kInvalidIndex);
			array_[idx].store(val, std::memory_order_relaxed);
		}

		Index Pop()
		{
			State state = state_.load(std::memory_order_relaxed);
			State new_state;
			Index val = kInvalidIndex;
			do
			{
				if (!state.size_)
				{
					return kInvalidIndex;
				}
				val = array_[state.first_].load(std::memory_order_relaxed);
				if (val == kInvalidIndex)
				{
					return kInvalidIndex;
				}
				new_state = State{ (state.first_ + 1) % Size,
					state.size_ - 1, state.tag_ + 1 };
			} while (!state_.compare_exchange_weak(state, new_state,
				std::memory_order_release,
				std::memory_order_relaxed));
			assert(array_[state.first_].load(std::memory_order_relaxed) == val);
			array_[state.first_].store(kInvalidIndex, std::memory_order_relaxed);
			return val;
		}


	private:
		struct State
		{
			Index first_ = 0;
			Index size_ = 0;
			Tag tag_ = 0;
		};
		std::array<std::atomic<Index>, Size> array_ = { kInvalidIndex };
		std::atomic<State> state_ = {};
	};
}