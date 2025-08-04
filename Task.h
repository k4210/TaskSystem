#pragma once

#include "Future.h"
#include "BaseTask.h"
#include "AccessSynchronizer.h"
#include <array>
#include <optional>
#include <type_traits>
#include <span>
#include <concepts>
#include <thread>

namespace ts
{
	class DetachHandle;
}

namespace ts
{
	template<typename T = void>
	class Task : public BaseTask, public CommonSpecialization<T, Task<T>>
	{};

	template<>
	class Task<void> : public BaseTask
	{
	public:
		using ReturnType = void;
	};

	class TaskSystem
	{
	public:
		static void WaitForAllTasks();

		static void StartWorkerThreads();

		static void StopWorkerThreadsNoWait();

		static void WaitForWorkerThreadsToJoin();

		static bool ExecuteATask(ETaskFlags flag, std::atomic<bool>& out_active);

		static void AsyncResume(DetachHandle handle LOCATION_PARAM);

		template<class F>
		static auto InitializeTask(F&& functor, std::span<Gate*> prerequiers = {}, ETaskFlags flags = ETaskFlags::None
			LOCATION_PARAM)
		{
			using ResultType = decltype(std::invoke(functor));
			static_assert(sizeof(Task<ResultType>) == sizeof(BaseTask));
			static_assert(sizeof(Future<ResultType>) == sizeof(GenericFuture));
			TRefCountPtr<BaseTask> task = CreateTask([function = std::forward<F>(functor)]([[maybe_unused]] BaseTask& task) mutable
				{
					if constexpr (std::is_void_v<ResultType>)
					{
						std::invoke( function);
					}
					else
					{
						task.result_.Store(std::invoke(function));
					}
				}, flags LOCATION_PASS);
			TaskSystem::HandlePrerequires(*task, prerequiers);
			return task.Cast<GenericFuture>().Cast<Future<ResultType>>();
		}

		template<class F, SyncT TValue>
		static auto InitializeTaskOn(F&& functor, SyncHolder<TValue> resource, ETaskFlags flags = ETaskFlags::None
			LOCATION_PARAM)
		{
			using ResultType = decltype(functor(AccessScope(resource.Get())));
			static_assert(sizeof(Task<ResultType>) == sizeof(BaseTask));
			static_assert(sizeof(Future<ResultType>) == sizeof(GenericFuture));

			struct LambdaObj
			{
				F function_;
				TValue* ptr_;
				BaseTask* task_ = nullptr;

				LambdaObj(F&& function, TValue* ptr) :
					function_(std::forward<F>(function)), ptr_(std::move(ptr))
				{}

				LambdaObj(LambdaObj&& moved) :
					function_(std::move(moved.function_)), ptr_(std::move(moved.ptr_))
				{
					moved.ptr_ = nullptr;
				}

				void operator()([[maybe_unused]] BaseTask& task)
				{
					assert(ptr_);
					assert(!AccessSynchronizer::is_any_asset_locked_); //If any other asset is locked it means there is a risk of deadlock
					DEBUG_CODE(AccessSynchronizer::is_any_asset_locked_ = true;)
						if constexpr (std::is_void_v<ResultType>)
						{
							std::invoke(function_, AccessScope(ptr_));
						}
						else
						{
							task.result_.Store(std::invoke(function_, AccessScope(ptr_)));
						}
					assert(AccessSynchronizer::is_any_asset_locked_);
					DEBUG_CODE(AccessSynchronizer::is_any_asset_locked_ = false;)
						assert(!task_);
					task_ = &task;
				}

				~LambdaObj()
				{
					if (ptr_)
					{
						assert(task_);
						ptr_->synchronizer_.ReleaseExclusive(*task_); //This works because Task::Execute cleans functor at the end
					}
					else
					{
						assert(!task_);
					}
				}
			};

			assert(resource.Get());
			AccessSynchronizer& synchronizer = resource.Get()->synchronizer_;
			TRefCountPtr<BaseTask> task = CreateTask(LambdaObj{ std::forward<F>(functor), std::move(resource.Get()) }, flags LOCATION_PASS);

			const AccessSynchronizer::SyncMultiResult sync_result = synchronizer.SyncExclusive(*task, task->GetTag());
			AccessSynchronizer::SyncMultiResult::HandleOnTask(sync_result, *task);

			return task.Cast<GenericFuture>().Cast<Future<ResultType>>();
		}

		template<typename T = void>
		static TRefCountPtr<Future<T>> MakeFuture()
		{
			static_assert(sizeof(GenericFuture) == sizeof(BaseFuture));
			static_assert(sizeof(BaseFuture) == sizeof(Future<T>));
			return MakeBaseFuture().Cast<Future<T>>();
		}

		static void HandlePrerequires(BaseTask& task, std::span<Gate*> prerequiers = {}, std::span<uint8> prerequiers_tags = {});

#pragma region private
	private:
		static TRefCountPtr<BaseTask> CreateTask(std::move_only_function<void(BaseTask&)> function,
			ETaskFlags flags = ETaskFlags::None LOCATION_PARAM);

		static TRefCountPtr<BaseFuture> MakeBaseFuture();

		static void OnReadyToExecute(TRefCountPtr<BaseTask> task);

		friend class BaseTask;
		template<typename T> friend class GuardedResource;
		template<SyncT TValue> friend struct AccessSynchronizerExclusiveTaskAwaiter;
		template<SyncT TValue> friend struct AccessSynchronizerSharedTaskAwaiter;
#pragma endregion
	};

}