#pragma once

#include "CoroutineHandle.h"
#include "Task.h"

namespace Coroutine
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

#define COROUTINE_CUSTOM_ALLOC 1

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

		auto await_transform(std::suspend_never InAwaiter)
		{
			return InAwaiter;
		}

		auto await_transform(std::suspend_always InAwaiter)
		{
			return InAwaiter;
		}

		template <typename SpecializedType, typename ReturnType = SpecializedType::ReturnType>
		auto await_transform(TRefCountPtr<SpecializedType>&& InTask)
		{
			using AsyncTask = TRefCountPtr<SpecializedType>;
			struct TTaskAwaiter
			{
				AsyncTask inner_task_;

				bool await_ready()
				{
					return !inner_task_->IsPendingOrExecuting();
				}
				void await_suspend(std::coroutine_handle<> handle)
				{
					assert(handle);
					auto resume_coroutine = [handle]()
						{
							handle.resume();
						};
					inner_task_->Then(resume_coroutine);
				}
				auto await_resume()
				{
					assert(!inner_task_->IsPendingOrExecuting());
					AsyncTask moved_task = std::move(inner_task_);
					inner_task_ = nullptr;
					if constexpr (!std::is_void_v<ReturnType>)
					{
						return moved_task->DropResult();
					}
				}
			};
			return TTaskAwaiter{ std::forward<AsyncTask>(InTask) };
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

		LockFree::GatedValue<std::coroutine_handle<>, detail::EInnerState> continuation_ =
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
}

template<typename ReturnType = void, typename YieldType = void>
using TUniqueCoroutine = Coroutine::TAttachPromise<ReturnType, YieldType>::TaskType;

using TDetachCoroutine = Coroutine::TDetachPromise::TaskType;