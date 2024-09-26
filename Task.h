#pragma once

#include "Future.h"
#include <array>
#include <optional>
#include <type_traits>
#include <span>
#include <concepts>
#include <thread>

class BaseTask : public GenericFuture
{
public:
	static std::span<BaseTask> GetPoolSpan();

	void OnPrerequireDone(TRefCountPtr<BaseTask>* out_first_ready_dependency);
	void Execute(TRefCountPtr<BaseTask>* out_first_ready_dependency = nullptr);

	ETaskFlags GetFlags() const { return flag_; }
#pragma region protected
protected:
	friend class TaskSystem;
	friend struct AccessSynchronizer;

	std::atomic<uint16> prerequires_ = 0;
	ETaskFlags flag_ = ETaskFlags::None;

	std::move_only_function<void(BaseTask&)> function_;
	DEBUG_CODE(std::source_location source;)
#pragma endregion
};

template<typename T = void>
class Task : public BaseTask, public CommonSpecialization<T, Task<T>>
{};

template<>
class Task<void> : public BaseTask
{
public:
	using ReturnType = void;
};

namespace Coroutine
{
	class DetachHandle;
}

class TaskSystem
{
public:
	static void WaitForAllTasks();

	static void StartWorkerThreads();

	static void StopWorkerThreads();

	static void WaitForWorkerThreadsToJoin();

	static bool ExecuteATask(ETaskFlags flag, std::atomic<bool>& out_active);

	static void AsyncResume(Coroutine::DetachHandle handle LOCATION_PARAM);

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
					std::invoke(function);
				}
				else
				{
					task.result_.Store(std::invoke(function));
				}
			}, flags LOCATION_PASS);
		TaskSystem::HandlePrerequires(*task, prerequiers);
		return task.Cast<GenericFuture>().Cast<Future<ResultType>>();
	}

	template<class F, class OPtr>
	static auto InitializeTaskOn(F&& functor, OPtr ptr, ETaskFlags flags = ETaskFlags::None
		LOCATION_PARAM)
	{
		using ResultType = decltype(functor(*ptr));
		static_assert(sizeof(Task<ResultType>) == sizeof(BaseTask));
		static_assert(sizeof(Future<ResultType>) == sizeof(GenericFuture));
		
		struct LambdaObj
		{
			F function_;
			OPtr ptr_;
			BaseTask* task_ = nullptr;

			LambdaObj(F&& function, OPtr ptr) :
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
				if constexpr (std::is_void_v<ResultType>)
				{
					std::invoke(function_, *ptr_);
				}
				else
				{
					task.result_.Store(std::invoke(function_, *ptr_));
				}
				assert(!task_);
				task_ = &task;
			}

			~LambdaObj()
			{
				if (ptr_)
				{
					assert(task_);
					ptr_->synchronizer_.Release(*task_);
				}
				else
				{
					assert(!task_);
				}
			}
		};

		assert(ptr);
		AccessSynchronizer& synchronizer = ptr->synchronizer_;
		TRefCountPtr<BaseTask> task = CreateTask(LambdaObj{std::forward<F>(functor), std::move(ptr)}, flags LOCATION_PASS);

		TRefCountPtr<BaseTask> prev_task_to_sync = synchronizer.Sync(*task);
		Gate* to_sync = prev_task_to_sync ? prev_task_to_sync->GetGate() : nullptr;
		Gate* pre_req[] = { to_sync };

		TaskSystem::HandlePrerequires(*task, pre_req);
		return task.Cast<GenericFuture>().Cast<Future<ResultType>>();
	}

	template<class F, class OPtr>
	static void InitializeResumeTaskOn(F&& functor, OPtr ptr, TRefCountPtr<BaseTask>& out_task, ETaskFlags flags = ETaskFlags::None
		LOCATION_PARAM)
	{
		using ResultType = void;
		static_assert(sizeof(Task<ResultType>) == sizeof(BaseTask));
		static_assert(sizeof(Future<ResultType>) == sizeof(GenericFuture));

		assert(ptr);
		AccessSynchronizer& synchronizer = ptr->synchronizer_;
		TRefCountPtr<BaseTask> task = CreateTask(
			[function = std::forward<F>(functor)](BaseTask&) mutable {std::invoke(function);}, 
			flags LOCATION_PASS);

		out_task = task;

		TRefCountPtr<BaseTask> prev_task_to_sync = synchronizer.Sync(*task);
		Gate* to_sync = prev_task_to_sync ? prev_task_to_sync->GetGate() : nullptr;
		Gate* pre_req[] = { to_sync };

		TaskSystem::HandlePrerequires(*task, pre_req);
	}

	template<typename T = void>
	static TRefCountPtr<Future<T>> MakeFuture()
	{
		static_assert(sizeof(GenericFuture) == sizeof(Future<T>));
		return MakeGenericFuture().Cast<Future<T>>();
	}

	static BaseTask* GetCurrentTask();

#pragma region private
private:
	static TRefCountPtr<BaseTask> CreateTask(std::move_only_function<void(BaseTask&)> function,
		ETaskFlags flags = ETaskFlags::None LOCATION_PARAM);

	static void HandlePrerequires(BaseTask& task, std::span<Gate*> prerequiers = {});

	static TRefCountPtr<GenericFuture> MakeGenericFuture();

	static void OnReadyToExecute(BaseTask&);

	friend class BaseTask;
	template<typename T> friend class GuardedResource;
#pragma endregion
};
