#pragma once

#include <coroutine>
#include <optional>

namespace Coroutine
{
	enum class EStatus
	{
		Unfinished,
		Done,
		Disconnected
	};

	// Not thread safe at all.
	template <typename Promise>
	class TUniqueHandle
	{
	public:
		using promise_type = Promise;
		using PromiseType = promise_type;
		using HandleType = std::coroutine_handle<PromiseType>;
		using ReturnType = PromiseType::ReturnType;
		using YieldType = PromiseType::YieldType;

		TUniqueHandle() = default;

		~TUniqueHandle()
		{
			assert(Status() != EStatus::Unfinished); //Not needed for simple generator
			Destroy();
		}

		TUniqueHandle(TUniqueHandle&& other)
			: handle_(std::move(other.handle_))
		{
			other.handle_ = nullptr;
		}

		TUniqueHandle& operator=(TUniqueHandle&& other)
		{
			assert(Status() != EStatus::Unfinished);
			Destroy();
			handle_ = std::move(other.handle_);
			other.handle_ = nullptr;
			return *this;
		}

		TUniqueHandle(const TUniqueHandle&) = delete;
		TUniqueHandle& operator=(const TUniqueHandle& other) = delete;

		bool TryResume()
		{
			if (handle_ && !handle_.done())
			{
				handle_.resume();
				return true;
			}
			return false;
		}

		void Destroy()
		{
			if (handle_)
			{
				handle_.destroy();
				handle_ = nullptr;
			}
		}

		// Thread safe
		EStatus Status() const
		{
			if (!handle_)
			{
				return EStatus::Disconnected;
			}
			const PromiseType& Promise = handle_.promise();
			return Promise.IsDone() 
				? EStatus::Done 
				: EStatus::Unfinished;
		}

		// Obtains the return value. 
		// Returns value only once after the task is done.
		// Any next call will return the empty value. 
		template <typename R = ReturnType, typename std::enable_if_t<!std::is_void<R>::value>* = nullptr>
		std::optional<ReturnType> Consume()
		{
			return handle_ ? handle_.promise().Consume() : std::optional<ReturnType>{};
		}

		template <typename U = YieldType, typename std::enable_if_t<!std::is_void<U>::value>* = nullptr>
		std::optional<YieldType> ConsumeYield()
		{
			return handle_ ? handle_.promise().ConsumeYield() : std::optional<YieldType>{};
		}

	private:
		TUniqueHandle(HandleType in_handle) : handle_(in_handle) {}

		PromiseType* GetPromise()
		{
			return handle_ ? &handle_.promise() : nullptr;
		}

		friend PromiseType;
		HandleType handle_;
	};
}
