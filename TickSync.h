#pragma once

#include "Task.h"
namespace ts
{
	struct TickSync
	{
		static inline constexpr uint32 kNumberOfFutures = 4;
		static inline constexpr uint32 kFutureOffset = 2;

		TickSync()
		{
			for (uint32 idx = 0; idx < kFutureOffset; idx++)
			{
				futures_[idx]  = TaskSystem::MakeFuture();
			}
		}

		// Returns what frame to wait for
		// Call on ticks, that are necessaty for next frame
		void RegisterNeededTick()
		{
			InnerUpdate([](State& state) { state.registered_ += 1; });
		}

		void UnregisterNeeded(/*uint16 first_missed_frame*/) //The entity does not wait.
		{
			InnerUpdate([](State& state) { state.registered_ -= 1; });
		}

		// frame_id will be incremented
		TRefCountPtr<Future<>> WaitForNextFrame(/*uint16& frame_id*/)
		{
			const uint32 future_idx = InnerUpdate([](State& state) { state.waiting_ += 1; });
			assert(futures_[future_idx]);
			return futures_[future_idx];
		}

	private:
		struct State
		{
			uint16 registered_ = 0;
			uint16 waiting_ = 0;
			uint32 frame_id_ = 0;
		};

		// returns future index
		uint32 InnerUpdate(auto functor)
		{
			State state = state_.load(std::memory_order_relaxed);
			State new_state;
			bool frame_ready = false;
			do
			{
				assert(state.registered_ >= state.waiting_);
				new_state = state;
				functor(new_state);
				assert(new_state.registered_ >= new_state.waiting_);
				frame_ready = new_state.registered_ == new_state.waiting_;
				if (frame_ready)
				{
					new_state.frame_id_ += 1;
					new_state.waiting_ = 0;
				}
			} while (!state_.compare_exchange_weak(state, new_state,
				std::memory_order_release,
				std::memory_order_relaxed));

			const uint32 future_idx = state.frame_id_ % kNumberOfFutures;
			if (frame_ready)
			{
				const uint32 reset_idx = (future_idx + kFutureOffset) % kNumberOfFutures;
				TRefCountPtr<Future<>>& reset_future = futures_[reset_idx];
				assert(!reset_future || !reset_future->IsPendingOrExecuting());
				assert(!reset_future || reset_future->GetGate()->IsEmpty());
				reset_future = TaskSystem::MakeFuture(); //let's hope noone is reading it anymore

				assert(futures_[future_idx] && futures_[future_idx]->IsPendingOrExecuting());
				futures_[future_idx]->Done();
			}

			return future_idx;
		}

		std::array<TRefCountPtr<Future<>>, kNumberOfFutures> futures_;
		std::atomic<State> state_;
	};

	struct TickScope
	{
		TickScope(TickSync& tick_sync)
			: tick_sync_(tick_sync)
		{
			tick_sync_.RegisterNeededTick();
		}

		TRefCountPtr<Future<>> WaitForNextFrame()
		{
			return tick_sync_.WaitForNextFrame();
		}

		~TickScope()
		{
			tick_sync_.UnregisterNeeded();
		}

	private:
		TickSync& tick_sync_;
	};
}