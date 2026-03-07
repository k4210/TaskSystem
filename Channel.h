#pragma once

#include "Gate.h"
#include "Task.h"
#include "SpinMutex.h"
#include <optional>

namespace ts
{
	template<typename T>
	struct ChannelReadResult
	{
		std::optional<T> value_;
		bool open_ = false;
	};

	struct ChannelWriteResult
	{
		bool written_ = false;
		bool open_ = false;
	};

	// Multi Producer Multi Consumer
	template<typename T, uint16_t InSize>
	class Channel
	{
	public:
		using Item = T;
		static constexpr uint16_t Size = InSize;

	private:
		struct State
		{
			uint16_t start_ = 0;
			uint16_t end_ = 0;
			bool open_ = true;
		};
		std::atomic<State> state_;

		std::array<SpinMutex, Size> locks_;
		std::array<Item, Size> items_;

		lock_free::Stack<Future<>> on_write_;
		lock_free::Stack<Future<>> on_read_;

	public:
		// Returns null when ready
		ChannelWriteResult Write(T& val)
		{
			State state = state_.load(std::memory_order_relaxed);
			State new_state;

			uint16_t index = std::numeric_limits<uint16_t>::max();

			do
			{

			} while (!state_.compare_exchange_weak(state, new_state,
				std::memory_order_release,
				std::memory_order_relaxed));
			


			return ChannelWriteResult { 
				.written_ = new_state.end_ != state.end_,
				.open = state_.load(std::memory_order_relaxed).open_
			};
		}

		ChannelReadResult<T> Read();

		//Single future will be triggered. Randomly ordered. 
		//Futures will be troggered also when the channel is closed.
		Future<> WaitForItem();
		Future<> WaitForSpace();

		void NotifyNextOneOnWrite();

		// No new item will be written
		// Disgard - Existing items are erased without reading
		void Close(bool disgard);

		bool IsOpen() const;
	};

	template<typename TChan>
	struct ChannelWriteAwaiter
	{
		TChan& channel_;
		TChan::Item value_ = {};
		ChannelWriteResult result_;

		std::coroutine_handle<> handle_;

		bool await_ready()
		{
			if (!result_.open_ || result_.written_)
				return true;
			assert(result_.notify_);
			return !result_.notify_->IsPendingOrExecuting();
		}

		void await_suspend(std::coroutine_handle<> handle)
		{
			assert(handle && !handle_);
			handle_ = handle;
			auto try_resume = [&]()
			{
				result_ = channel_.Write(value_);
				if (await_ready())
					handle_.resume();
				else
					result_.future_->Then(try_resume);
			};
			result_.future_->Then(try_resume);
		}

		auto await_resume()
		{
			return result_.written_;
		}
	};

	template<typename TChan>
	struct ChannelReadAwaiter
	{
		TChan& channel_;
		ChannelReadResult<typename TChan::Item> result_;

		std::coroutine_handle<> handle_;

		bool await_ready()
		{
			if (!result_.open_ || result_.value_)
				return true;
			assert(result_.notify_);
			return !result_.notify_->IsPendingOrExecuting();
		}

		void await_suspend(std::coroutine_handle<> handle)
		{
			assert(handle && !handle_);
			handle_ = handle;
			auto try_resume = [&]()
			{
				result_ = channel_.Read();
				if (await_ready())
					handle_.resume();
				else
					result_.future_->Then(try_resume);
			};
			result_.future_->Then(try_resume);
		}

		auto await_resume()
		{
			return result_.value_;
		}
	};


	// Multiplexer
}