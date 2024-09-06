#pragma once

#include "TaskBaseTypes.h"
#include <array>
#include <optional>
#include <type_traits>
#include <span>

template<typename T, typename DerivedType>
class CommonSpecialization
{
public:
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
	auto ThenRead(F function, const char* debug_name = nullptr, ETaskFlags flags = ETaskFlags::None) -> TRefCountPtr < Task<decltype(function(T{})) >>
	{
		DerivedType* common = static_cast<DerivedType*>(this);
		Gate* pre_req[] = { common->GetGate() };
		using ResultType = decltype(function(T{}));
		auto lambda = [source = TRefCountPtr<DerivedType>(common), function = std::move(function)]() -> ResultType
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
		return TaskSystem::InitializeTask(std::move(lambda), pre_req, debug_name, flags);
	}

	template<typename F>
	auto ThenConsume(F function, const char* debug_name = nullptr, ETaskFlags flags = ETaskFlags::None) -> TRefCountPtr < Task<decltype(function(T{})) >>
	{
		DerivedType* common = static_cast<DerivedType*>(this);
		Gate* pre_req[] = { common->GetGate() };
		using ResultType = decltype(function(T{}));
		auto lambda = [source = TRefCountPtr<DerivedType>(common), function = std::move(function)]() -> ResultType
			{
				T value = source->DropResult();
				//source = nullptr; TODO
				if constexpr (std::is_void_v<ResultType>)
				{
					function(value);
					return;
				}
				else
				{
					return function(value);
				}
			};
		return TaskSystem::InitializeTask(std::move(lambda), pre_req, debug_name, flags);
	}
};

template<typename T = void>
class Task : public BaseTask, public CommonSpecialization<T, Task<T>>
{};

template<>
class Task<void> : public BaseTask
{};

class TaskSystem
{
public:
	static void ResetGlobals();

	static void StartWorkerThreads();

	static void StopWorkerThreads();

	static void WaitForWorkerThreadsToJoin();

	static bool ExecuteATask();

	template<typename F>
	static auto InitializeTask(F function, std::span<Gate*> prerequiers = {}, const char* debug_name = nullptr, ETaskFlags flags = ETaskFlags::None)
		-> TRefCountPtr<Task<decltype(function())>>
	{
		using ResultType = decltype(function());
		static_assert(sizeof(Task<ResultType>) == sizeof(BaseTask));
		if constexpr (std::is_void_v<ResultType>)
		{
			return InitializeTaskInner([function = std::move(function)](BaseTask&)
				{
					function();
				}, prerequiers, debug_name, flags).Cast<Task<ResultType>>();
		}
		else
		{
			return InitializeTaskInner([function = std::move(function)](BaseTask& task)
				{
					task.result_.Store(function());
				}, prerequiers, debug_name, flags).Cast<Task<ResultType>>();
		}
	}
#pragma region private
private:
	static TRefCountPtr<BaseTask> InitializeTaskInner(std::function<void(BaseTask&)> function,
		std::span<Gate*> prerequiers = {}, const char* debug_name = nullptr, ETaskFlags flags = ETaskFlags::None);

	static void OnReadyToExecute(BaseTask&);

	friend class BaseTask;
#pragma endregion
};
