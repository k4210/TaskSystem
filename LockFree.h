#pragma once

#include <atomic>
#include <span>
#include <limits>
#include <optional>
#include <assert.h>
#include <array>
#include "Common.h"

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
			assert([&]() -> bool
				{
					const Index wanted = GetPoolIndex(chain_tail);
					Index local = GetPoolIndex(new_head);
					for (int32 counter = 0; counter < 2048; counter++)
					{
						if (local == wanted)
						{
							return true;
						}
						if (local == kInvalidIndex)
						{
							break;
						}
						local = FromPoolIndex<Node>(local).next_;
					}
					return false;
				}());
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
		bool Add(Node& node, const Gate required_open)
		{
			const Index idx = GetPoolIndex(node);
			const State new_state{ .head = idx, .gate = required_open };
			State state = state_.load(std::memory_order_relaxed);
			do
			{
				if (state.gate != required_open)
				{
					return false;
				}
				node.next_ = state.head;
			} while (!state_.compare_exchange_weak(state, new_state,
				std::memory_order_release,
				std::memory_order_relaxed));
			return true;
		}

		// Returns old gate
		Gate AddForceOpen(Node& node, const Gate open)
		{
			const Index idx = GetPoolIndex(node);
			const State new_state{ .head = idx, .gate = open };
			State state = state_.load(std::memory_order_relaxed);
			do
			{
				node.next_ = state.head;
			} while (!state_.compare_exchange_weak(state, new_state,
				std::memory_order_release,
				std::memory_order_relaxed));
			return state.gate;
		}

		//returns prev state
		Gate ExchangeState(Gate new_gate)
		{
			State state = state_.load(std::memory_order_relaxed);
			State new_state{ state.head, new_gate };
			do
			{
				if (state.gate == new_gate)
				{
					break;
				}
				new_state.head = state.head;
			} while (!state_.compare_exchange_weak(state, new_state,
				std::memory_order_release,
				std::memory_order_relaxed));
			return state.gate;
		}

		bool ChangeIfEmpty(Gate new_gate)
		{
			const State new_state{ kInvalidIndex, new_gate };
			State state = state_.load(std::memory_order_relaxed);
			do
			{
				if (state.head != kInvalidIndex)
				{
					return false;
				}
				assert(state.gate != new_gate);
			} while (!state_.compare_exchange_weak(state, new_state,
				std::memory_order_release,
				std::memory_order_relaxed));
			return true;
		}

		//Should be called once. Returns prev gate state
		template<typename Func>
		Gate CloseAndConsume(const Gate closed, Func& func)
		{
			const State old_state = state_.exchange(State{ .gate = closed });

			Index head = old_state.head;
			while (head != kInvalidIndex)
			{
				Node& node = FromPoolIndex<Node>(head);
				head = node.next_;
				func(node); // node.next_ should be handled here
			}
			return old_state.gate;
		}

	private:
		State GetState() const
		{
			return state_.load(
				//std::memory_order_relaxed
			);
		}

		std::atomic<State> state_;
	};

	// Allows to set value, only when gate is in open state
	template<typename Value, typename Gate>
	struct GatedValue
	{
		struct State
		{
			Value value_;
			Gate gate_;
		};

		GatedValue(Value val, Gate gate)
			: state_(State{ val, gate })
		{}

		bool TrySet(const Gate required_open, Value new_value, const Value assert_empty = {})
		{
			const State new_state{ std::move(new_value), required_open };
			State state = state_.load(std::memory_order_relaxed);
			do
			{
				if (state.gate_ != required_open)
				{
					return false;
				}
				assert(assert_empty == state.value_);
			} while (!state_.compare_exchange_weak(state, new_state,
				std::memory_order_release,
				std::memory_order_relaxed));
			return true;
		}

		State Close(const Gate new_closed, Value new_empty)
		{
			return state_.exchange(State{ std::move(new_empty), new_closed });
		}

		State Get() const
		{
			return state_.load(std::memory_order_relaxed);
		}

	private:
		std::atomic<State> state_;
	};
}