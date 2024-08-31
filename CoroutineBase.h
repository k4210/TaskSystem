#pragma once

#include <coroutine>
#include <optional>

namespace Coroutine
{
	using FSuspendAlways = std::suspend_always;
	using FSuspendNever = std::suspend_never;

	enum class EStatus
	{
		Unfinished,
		Done,
		Disconnected
	};

	template <typename Return>
	class TPromiseReturn
	{
	public:
		using ReturnType = Return;

		void return_value(const Return& InValue)
		{
			ValueReturn = InValue;
		}
		void return_value(Return&& InValue)
		{
			ValueReturn = std::forward<Return>(InValue);
		}
		void return_value(std::optional<Return>&& InValue)
		{
			ValueReturn = std::forward<std::optional<Return>>(InValue);
		}

		bool HasReturnValue() const
		{
			return !!ValueReturn;
		}

		std::optional<Return> Consume()
		{
			std::optional<Return> result = std::move(ValueReturn);
			ValueReturn.reset();
			return result;
		}
	protected:
		std::optional<Return> ValueReturn;
	};

	template <>
	class TPromiseReturn<void>
	{
	public:
		using ReturnType = void;
		void return_void() {}
	};

	template <typename Return, typename Yield>
	class TPromiseYield : public TPromiseReturn<Return>
	{
	public:
		using YieldType = Yield;

		FSuspendAlways yield_value(const Yield& InValue)
		{
			ValueYield = InValue;
			return {};
		}
		FSuspendAlways yield_value(Yield&& InValue)
		{
			ValueYield = std::forward<Yield>(InValue);
			return {};
		}
		std::optional<Yield> ConsumeYield()
		{
			std::optional<Yield> result = std::move(ValueYield);
			ValueYield.reset();
			return result;
		}
	protected:
		std::optional<Yield> ValueYield;
	};

	template <typename Return>
	class TPromiseYield<Return, void> : public TPromiseReturn<Return> 
	{
	public:
		using YieldType = void;
	};

	template <typename Promise>
	class TUniqueHandle
	{
		void DestroyCoroutine()
		{
			if (PromiseType* promise = GetPromise())
			{
				if constexpr (requires { promise->NotifyDestruction(); })
				{
					promise->NotifyDestruction();
				}
				handle_.destroy();
				handle_ = nullptr;
			}
			else
			{
				assert(!handle_);
			}
		}

	public:
		using promise_type = Promise;
		using PromiseType = promise_type;
		using HandleType = std::coroutine_handle<PromiseType>;
		using ReturnType = PromiseType::ReturnType;
		using YieldType = PromiseType::YieldType;

		TUniqueHandle() = default;

		~TUniqueHandle()
		{
			DestroyCoroutine();
		}

		TUniqueHandle(TUniqueHandle&& other)
			: handle_(std::move(other.handle_))
		{
			other.handle_ = nullptr;
		}

		TUniqueHandle& operator=(TUniqueHandle&& other)
		{
			DestroyCoroutine();
			handle_ = std::move(other.handle_);
			other.handle_ = nullptr;
			return *this;
		}

		bool TryResume()
		{
			if (handle_ && !handle_.done())
			{
				handle_.resume();
				return true;
			}
			return false;
		}

		EStatus Status() const
		{
			const PromiseType* Promise = GetPromise();
			return Promise ? Promise->Status() : EStatus::Disconnected;
		}

		// Obtains the return value. 
		// Returns value only once after the task is done.
		// Any next call will return the empty value. 
		template <typename U = ReturnType, typename std::enable_if_t<!std::is_void<U>::value>* = nullptr>
		std::optional<ReturnType> Consume()
		{
			PromiseType* Promise = GetPromise();
			return Promise ? Promise->Consume() : std::optional<ReturnType>{};
		}

		template <typename U = YieldType, typename std::enable_if_t<!std::is_void<U>::value>* = nullptr>
		std::optional<YieldType> ConsumeYield()
		{
			PromiseType* Promise = GetPromise();
			return Promise ? Promise->ConsumeYield() : std::optional<YieldType>{};
		}

	protected:

		friend PromiseType;

		TUniqueHandle(HandleType in_handle) : handle_(in_handle) {}

		TUniqueHandle(const TUniqueHandle&) = delete;
		TUniqueHandle& operator=(const TUniqueHandle& other) = delete;

		PromiseType* GetPromise() const
		{
			return handle_ ? &handle_.promise() : nullptr;
		}

		HandleType handle_;
	};
}
