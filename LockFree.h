#pragma once

#include <atomic>
#include <span>
#include <optional>
#include <assert.h>
#include <array>
#include "Common.h"

namespace ts::lock_free
{
	using Tag = uint16;

	template<typename Node>
	struct Stack
	{
		using IndexType = decltype(Node::next_);

		void Push(Node& node)
		{
			const IndexType idx{ GetPoolIndex(node) };
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
					const IndexType wanted{ GetPoolIndex(chain_tail) };
					IndexType local{ GetPoolIndex(new_head) };
					for (int32 counter = 0; counter < (8 * 1024); counter++)
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
			const IndexType idx{ GetPoolIndex(new_head) };
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
				if (state.head == kInvalidIndex)
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
		IndexType Reset(IndexType new_head = kInvalidIndex)
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
			IndexType head{ kInvalidIndex };
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

	enum class ETagAction
	{
		None, 
		Increment,
		Reset
	};

	template<typename Node, typename Gate>
	struct Collection
	{
		using IndexType = decltype(Node::next_);
		struct State
		{
			IndexType head{ kInvalidIndex };
			Gate gate = {};
			uint8 tag = 0;

			bool operator== (const State&) const = default;
		};

		Collection(Gate gate)
			: state_(State{ .gate = gate })
		{}

		bool IsEmpty() const
		{
			return GetState().head == kInvalidIndex;
		}

		// Returns old gate
		State SetOnEmpty(const Gate new_gate, const ETagAction tag_action = ETagAction::None)
		{
			const State old_state = ResetInner(new_gate, tag_action);
			assert(old_state.head == kInvalidIndex);
			return old_state;
		}

		//return if the element was added
		bool Add(Node& node, const Gate required_open, const uint8 required_tag)
		{
			const IndexType idx{ GetPoolIndex(node) };
			const State new_state{ idx, required_open, required_tag };
			State old_state = state_.load(std::memory_order_relaxed);
			do
			{
				if (old_state.gate != required_open || old_state.tag != required_tag)
				{
					return false;
				}
				node.next_ = old_state.head;
			} while (!state_.compare_exchange_weak(old_state, new_state,
				std::memory_order_release,
				std::memory_order_relaxed));
			return true;
		}

		bool Add(Node& node, const Gate required_open)
		{
			const IndexType idx{ GetPoolIndex(node) };
			State new_state{ idx, required_open };
			State old_state = state_.load(std::memory_order_relaxed);
			do
			{
				if (old_state.gate != required_open)
				{
					return false;
				}
				new_state.tag = old_state.tag;
				node.next_ = old_state.head;
			} while (!state_.compare_exchange_weak(old_state, new_state,
				std::memory_order_release,
				std::memory_order_relaxed));
			return true;
		}

		//Should be called once. Returns prev state
		template<typename Func>
		State ConsumeAll(const Gate new_gate, Func& func, const ETagAction tag_action = ETagAction::None)
		{
			const State old_state = ResetInner(new_gate, tag_action);

			IndexType head = old_state.head;
			while (head != kInvalidIndex)
			{
				Node& node = FromPoolIndex<Node>(head);
				head = node.next_;
				func(node); // node.next_ should be handled here
			}
			return old_state;
		}

		State GetState() const
		{
			return state_.load(
				std::memory_order_relaxed
			);
		}
	private:
		State ResetInner(const Gate new_gate, const ETagAction tag_action)
		{
			State new_state{ .gate = new_gate };
			State old_state = state_.load(std::memory_order_relaxed);
			if (tag_action == ETagAction::Reset)
			{
				old_state = state_.exchange(new_state, std::memory_order_relaxed);
			}
			else
			{
				do
				{
					new_state.tag = (tag_action == ETagAction::Increment)
						? (old_state.tag + 1) : old_state.tag;
				} while (!state_.compare_exchange_weak(old_state, new_state,
					std::memory_order_release,
					std::memory_order_relaxed));
			}
			return old_state;
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