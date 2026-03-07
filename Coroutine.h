#pragma once

#include "CoroutineHandle.h"
#include "Task.h"
#include "Future.h"
#include "AccessSynchronizer.h"
#include "Channel.h"

namespace ts
{
	namespace detail
	{
		void* do_allocate(std::size_t bytes);
		void do_deallocate(void* p);
		void ensure_allocator_free();
		enum class EInnerState : uint8 { Unfinished, Done };
	}

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

		std::suspend_always yield_value(const Yield& InValue)
		{
			ValueYield = InValue;
			return {};
		}
		std::suspend_always yield_value(Yield&& InValue)
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

	template <typename Return = void, typename Yield = void>
	class TPromise : public TPromiseYield<Return, Yield>
	{
	public:
#if COROUTINE_CUSTOM_ALLOC
		void* operator new(std::size_t size)
		{
			return detail::do_allocate(size);
		}

		void operator delete (void* ptr)
		{
			detail::do_deallocate(ptr);
		}
#endif //COROUTINE_CUSTOM_ALLOC

		void unhandled_exception() { assert(false); }

		auto await_transform(std::suspend_never InAwaiter) { return InAwaiter; }

		auto await_transform(std::suspend_always InAwaiter) { return InAwaiter; }

		template <std::derived_from<GenericFuture> SpecializedType>
		auto await_transform(TRefCountPtr<SpecializedType>&& InTask)
		{
			return GenericFutureAwaiter<SpecializedType>{ InTask };
		}

		template <typename OtherPromise>
		auto await_transform(TUniqueHandle<OtherPromise>&& in_coroutine)
		{
			struct CoroutineAwaiter
			{
				using ReturnType = OtherPromise::ReturnType;

				TUniqueHandle<OtherPromise> awaited_;

				bool await_ready()
				{
					return awaited_.Status() != EStatus::Unfinished;
				}

				void await_suspend(std::coroutine_handle<> handle)
				{
					assert(handle);
					OtherPromise* promise = awaited_.GetPromise();
					assert(promise);
					const bool passed = promise->continuation_.TrySet(detail::EInnerState::Unfinished, handle);
					if (!passed)
					{
						handle.resume();
					}
				}

				auto await_resume()
				{
					if constexpr (!std::is_void_v<ReturnType>)
					{
						assert(awaited_.GetPromise() && awaited_.GetPromise()->HasReturnValue());
						return awaited_.Consume().value();
					}
				}
			};
			return CoroutineAwaiter{ std::forward<TUniqueHandle<OtherPromise>>(in_coroutine) };
		}

		template<SyncT TValue>
		auto await_transform(SyncHolder<TValue> resource)
		{
			return AccessSynchronizerExclusiveTaskAwaiter<TValue>( std::forward<SyncHolder<TValue>>(resource) );
		}

		template<SyncT TValue>
		auto await_transform(SharedSyncHolder<TValue> resource)
		{
			return AccessSynchronizerSharedTaskAwaiter<TValue>( std::forward<SharedSyncHolder<TValue>>(resource) );
		}

		template<typename T>
		auto await_transform(ChannelReadResult<T> result)
		{
			return ChannelReadAwaiter<T>(std::move(result));
		}
	};

	template <typename Return = void, typename Yield = void>
	class TAttachPromise : public TPromise<Return, Yield>
	{
	public:
		using HandleType = std::coroutine_handle<TAttachPromise>;
		using TaskType = TUniqueHandle<TAttachPromise>;

		bool IsDone() const
		{
			return continuation_.Get().gate_ == detail::EInnerState::Done;
		}

		auto initial_suspend() { return std::suspend_never{}; }

		auto final_suspend() noexcept
		{
			struct FFinalAwaiter
			{
				bool await_ready() noexcept { return false; }

				void await_suspend(HandleType handle) noexcept
				{
					std::coroutine_handle<> continuation = handle.promise().continuation_.
						Close(detail::EInnerState::Done, std::coroutine_handle<>{}).value_;
					if (continuation)
					{
						continuation.resume();
					}
				}

				void await_resume() noexcept {}
			};
			return FFinalAwaiter{};
		}

		auto get_return_object()
		{
			return TaskType(HandleType::from_promise(*this));
		}

		lock_free::GatedValue<std::coroutine_handle<>, detail::EInnerState> continuation_ =
			{ std::coroutine_handle<>{}, detail::EInnerState::Unfinished };
	};

	class TDetachPromise : public TPromise<void, void>
	{
	public:
		using HandleType = std::coroutine_handle<TDetachPromise>;
		using TaskType = TDetachHandle<TDetachPromise>;

		auto initial_suspend() { return std::suspend_always{}; }

		auto final_suspend() noexcept
		{
			return std::suspend_never{};
		}

		auto get_return_object()
		{
			return TaskType(HandleType::from_promise(*this));
		}
	};

	template<typename ReturnType = void, typename YieldType = void>
	using TUniqueCoroutine = TAttachPromise<ReturnType, YieldType>::TaskType;

	using TDetachCoroutine = TDetachPromise::TaskType;
}

