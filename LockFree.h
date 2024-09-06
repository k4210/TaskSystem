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

	template<typename Node>
	struct PointerBasedStack
	{
		void Push(Node& node)
		{
			State new_state{ .head = &node };
			State state = state_.load(std::memory_order_relaxed);
			do
			{
				new_state.tag = state.tag + 1;
				node.next_ = state.head;
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
				if (nullptr == state.head)
				{
					return nullptr;
				}
				new_state.head = state.head->next_;
				new_state.tag = state.tag + 1;
			} while (!state_.compare_exchange_weak(state, new_state,
				std::memory_order_release,
				std::memory_order_relaxed));

			state.head->next_ = nullptr;
			return state.head;
		}

	private:
		struct State
		{
			Node* head = nullptr;
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

		Gate GetGateState() const
		{
			return GetState().gate;
		}

		bool IsEmpty() const
		{
			return GetState().head == kInvalidIndex;
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
		State GetState() const
		{
			return state_.load(std::memory_order_relaxed);
		}

		std::atomic<State> state_;
	};

}