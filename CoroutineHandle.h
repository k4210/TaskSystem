#pragma once

#include <coroutine>
#include <optional>
#include "assert.h"

namespace Coroutine
{
	template <typename Promise> class TDetachHandle;

	class DetachHandle
	{
	public:
		DetachHandle(std::coroutine_handle<> in_handle)
			: handle_(in_handle)
		{
			assert(handle_);
		}

		DetachHandle(const DetachHandle&) = delete;
		DetachHandle& operator=(const DetachHandle&) = delete;
		DetachHandle(DetachHandle&& moved)
			: handle_(moved.handle_)
		{
			moved.handle_ = nullptr;
		}
		template<typename Promise>
		DetachHandle(TDetachHandle<Promise>&& moved)
			: handle_(moved.handle_)
		{
			moved.handle_ = nullptr;
		}
		DetachHandle& operator=(DetachHandle&&) = delete;

		void ResumeAndDetach()
		{
			assert(handle_);
			handle_.resume();
			handle_ = nullptr;
		}

		~DetachHandle()
		{
			assert(!handle_);
		}

	private:
		std::coroutine_handle<> handle_;
	};

	template <typename Promise>
	class TDetachHandle : public DetachHandle
	{
	public:
		using promise_type = Promise;

		TDetachHandle(std::coroutine_handle<Promise> in_handle)
			: DetachHandle(in_handle)
		{
			static_assert(sizeof(TDetachHandle<Promise>) == sizeof(DetachHandle));
		}
	};

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

		EStatus Status() const
		{
			if (!handle_)
			{
				return EStatus::Disconnected;
			}
			const PromiseType& promise = handle_.promise();
			return promise.IsDone() // Thread safe
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
		template <typename R, typename Y> friend class TPromise;
		HandleType handle_;
	};
}
