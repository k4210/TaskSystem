#pragma once

#include "CoroutineBase.h"
#include "Task.h"

namespace Coroutine
{
	namespace detail
	{
		void* do_allocate(std::size_t bytes);
		void do_deallocate(void* p);
	}

#define COROUTINE_CUSTOM_ALLOC 1

	template <typename Return = void, typename Yield = void>
	class TPromise : public TPromiseYield<Return, Yield>
	{
		using HandleType = std::coroutine_handle<TPromise>;

		HandleType GetHandle()
		{
			return HandleType::from_promise(*this);
		}

	public:
		using TaskType = TUniqueHandle<TPromise>;
#if COROUTINE_CUSTOM_ALLOC
		void* operator new(std::size_t size)
		{
			return detail::do_allocate(size);
		}

		void operator delete (void* ptr)
		{
			detail::do_deallocate(ptr);
		}

		static TaskType get_return_object_on_allocation_failure()
		{
			assert(false);
			return TaskType();
		}
#endif //COROUTINE_CUSTOM_ALLOC

		EStatus Status() const
		{
			const HandleType handle = const_cast<TPromise*>(this)->GetHandle();
			return handle.done() ? EStatus::Done : EStatus::Unfinished;
		}

		auto get_return_object()
		{
			return TaskType(GetHandle());
		}

		auto initial_suspend() { return FSuspendNever{}; }

		void unhandled_exception() {}

		auto final_suspend() noexcept
		{
			struct FFinalAwaiter
			{
				bool await_ready() noexcept { return false; }

				auto await_suspend(HandleType handle) noexcept
				{
					std::coroutine_handle<>& continuation = handle.promise().continuation_;
					if (continuation)
					{
						continuation.resume();
						/*
						TaskSystem::InitializeTask([handle = continuation]()
							{
								handle.resume();
							}, {}, "final suspend continuation");
						continuation = std::coroutine_handle<>{};
						*/
					}
				}

				void await_resume() noexcept {}
			};
			return FFinalAwaiter{};
		}

		auto await_transform(FSuspendNever InAwaiter)
		{
			return InAwaiter;
		}

		auto await_transform(FSuspendAlways InAwaiter)
		{
			return InAwaiter;
		}

		template <typename ReturnType>
		auto await_transform(TRefCountPtr<Task<ReturnType>>&& InTask)
		{
			using AsyncTask = TRefCountPtr<Task<ReturnType>>;
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
					inner_task_->Then(resume_coroutine, "resume_coroutine");
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
			return TTaskAwaiter{std::forward<AsyncTask>(InTask)};
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
					assert(!promise->continuation_);
					promise->continuation_ = handle;
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
			assert(in_coroutine.GetPromise());
			return CoroutineAwaiter{ std::forward<TUniqueHandle<OtherPromise>>(in_coroutine) };
		}
	protected:
		std::coroutine_handle<> continuation_;
	};
}

template<typename ReturnType = void>
using TUniqueCoroutine = Coroutine::TPromise<ReturnType>::TaskType;