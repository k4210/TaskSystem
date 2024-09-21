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

	std::atomic<uint16> prerequires_ = 0;
	ETaskFlags flag_ = ETaskFlags::None;

	std::function<void(BaseTask&)> function_;
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
		return InitializeTaskInner([function = std::forward<F>(functor)]([[maybe_unused]] BaseTask& task) mutable
			{
				if constexpr (std::is_void_v<ResultType>)
				{
					std::invoke(function);
				}
				else
				{
					task.result_.Store(std::invoke(function));
				}
			},
			prerequiers, flags LOCATION_PASS).Cast<GenericFuture>().Cast<Future<ResultType>>();
	}

	template<typename T = void>
	static TRefCountPtr<Future<T>> MakeFuture()
	{
		static_assert(sizeof(GenericFuture) == sizeof(Future<T>));
		return MakeGenericFuture().Cast<Future<T>>();
	}

#pragma region private
private:
	static TRefCountPtr<BaseTask> InitializeTaskInner(std::function<void(BaseTask&)> function,
		std::span<Gate*> prerequiers = {}, ETaskFlags flags = ETaskFlags::None LOCATION_PARAM);

	static TRefCountPtr<GenericFuture> MakeGenericFuture();

	static void OnReadyToExecute(BaseTask&);

	friend class BaseTask;
	template<typename T> friend class GuardedResource;
#pragma endregion
};
