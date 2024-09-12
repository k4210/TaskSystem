#pragma once

#include "BaseTask.h"
#include <array>
#include <optional>
#include <type_traits>
#include <span>
#include <concepts>

template<typename T, typename DerivedType>
class CommonSpecialization
{
public:
	using ReturnType = T;

	// Either ShareResult or DropResult should be used, no both!
	T DropResult()
	{
		DerivedType* common = static_cast<DerivedType*>(this);
		const ETaskState old_state = common->gate_.ResetStateOnEmpty(ETaskState::Done);
		assert(old_state == ETaskState::DoneUnconsumedResult);
		T result = common->result_.GetOnce<T>();
		common->result_.Reset();
		return result;
	}

	const T& ShareResult() const
	{
		const DerivedType* common = static_cast<const DerivedType*>(this);
		assert(common->gate_.GetState() == ETaskState::DoneUnconsumedResult);
		return common->result_.Get<T>();
	}

	template<typename F>
	auto ThenRead(F&& function, ETaskFlags flags = ETaskFlags::None LOCATION_PARAM)
	{
		DerivedType* common = static_cast<DerivedType*>(this);
		Gate* pre_req[] = { common->GetGate() };
		using ResultType = decltype(function(T{}));
		auto lambda = [source = TRefCountPtr<DerivedType>(common), function = std::forward<F>(function)]() -> ResultType
			{
				if constexpr (std::is_void_v<ResultType>)
				{
					function(source->ShareResult());
					return;
				}
				else
				{
					return function(source->ShareResult());
				}
			};
		return TaskSystem::InitializeTask(std::move(lambda), pre_req, flags LOCATION_PASS);
	}

	template<typename F>
	auto ThenConsume(F&& function, ETaskFlags flags = ETaskFlags::None LOCATION_PARAM)
	{
		DerivedType* common = static_cast<DerivedType*>(this);
		Gate* pre_req[] = { common->GetGate() };
		using ResultType = decltype(function(T{}));
		auto lambda = [source = TRefCountPtr<DerivedType>(common), function = std::forward<F>(function)]() mutable -> ResultType
			{
				T value = source->DropResult();
				source = nullptr;
				if constexpr (std::is_void_v<ResultType>)
				{
					function(std::move(value));
					return;
				}
				else
				{
					return function(std::move(value));
				}
			};
		return TaskSystem::InitializeTask(std::move(lambda), pre_req, flags LOCATION_PASS);
	}
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

template<typename T = void>
class Future : public GenericFuture, public CommonSpecialization<T, Future<T>>
{
public:
	void Done(T&& val)
	{
		assert(gate_.GetState() == ETaskState::PendingOrExecuting);
		assert(!result_.HasValue());
		result_.Store(std::forward<T>(val));
		gate_.Done(ETaskState::DoneUnconsumedResult);
	}
};

template<>
class Future<void> : public GenericFuture
{
public:
	using ReturnType = void;

	void Done()
	{
		assert(gate_.GetState() == ETaskState::PendingOrExecuting);
		assert(!result_.HasValue());
		gate_.Done(ETaskState::Done);
	}
};


class TaskSystem
{
public:
	static void WaitForAllTasks();

	static void StartWorkerThreads();

	static void StopWorkerThreads();

	static void WaitForWorkerThreadsToJoin();

	static bool ExecuteATask();

	static auto InitializeTask(std::invocable auto&& function, std::span<Gate*> prerequiers = {}, ETaskFlags flags = ETaskFlags::None
		LOCATION_PARAM)
	{
		using ResultType = decltype(std::invoke(function));
		static_assert(sizeof(Task<ResultType>) == sizeof(BaseTask));
		if constexpr (std::is_void_v<ResultType>)
		{
			return InitializeTaskInner([function = function](BaseTask&) mutable
				{
					std::invoke(function);
				}, prerequiers, flags LOCATION_PASS).Cast<Task<ResultType>>();
		}
		else
		{
			return InitializeTaskInner([function = function](BaseTask& task) mutable
				{
					task.result_.Store(std::invoke(function));
				}, prerequiers, flags LOCATION_PASS).Cast<Task<ResultType>>();
		}
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
#pragma endregion
};
