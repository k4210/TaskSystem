#pragma once

#include <functional>
#include <array>
#include <span>
#include "RefCount.h"
#include "RefCountPoolPtr.h"
#include "LockFree.h"
#include "Pool.h"
#include "AnyValue.h"
#include <optional>
#include <type_traits>

struct DependencyNode;
template<typename T> class Task;

enum class ETaskState : uint8
{
#if !defined(NDEBUG)
	Nonexistent_Pooled,
#endif
	PendingOrExecuting,
	Done,
	DoneUnconsumedResult,
};

class BaseTask : public TRefCounted<BaseTask>
{
public:
	BaseTask();

	static std::span<BaseTask> GetPoolSpan();

	bool IsDone() const;

	bool HasUnconsumedResult() const;

	uint16 GetNumberOfPendingPrerequires() const;

	template<typename F>
	auto Then(F function, const char* debug_name = nullptr) -> TRefCountPtr<Task<decltype(function())>>
	{
		BaseTask* pre_req[] = { this };
		return TaskSystem::InitializeTask(std::move(function), pre_req, debug_name);
	}
#pragma region protected
protected:

	void Execute();
#if !defined(NDEBUG)
	void OnReturnToPool();
#endif
	void OnRefCountZero();

	void OnPrerequireDone();

	std::atomic<uint16> prerequires_ = 0;
	LockFree::Index next_ = LockFree::kInvalidIndex;
	LockFree::Collection<DependencyNode, ETaskState> depending_;

	std::function<void(BaseTask&)> function_;
	AnyValue<6 * sizeof(uint8*)> result_;
	const char* debug_name_ = nullptr;

	friend LockFree::Stack<BaseTask>;
	template<typename Node, std::size_t Size> friend struct Pool;
	friend TRefCounted<BaseTask>;
	friend class TaskSystem;
#pragma endregion
};

template<typename T = void>
class Task : public BaseTask
{
public:

	// Either ShareResult or DropResult should be used, no both!
	T DropResult()
	{
		const ETaskState old_state = depending_.SetFastOnEmpty(ETaskState::Done);
		assert(old_state == ETaskState::DoneUnconsumedResult);
		return result_.GetOnce<T>();
	}

	const T& ShareResult() const
	{
		assert(depending_.GetGateState() == ETaskState::DoneUnconsumedResult);
		return result_.Get<T>();
	}

	template<typename F>
	auto ThenRead(F function, const char* debug_name = nullptr) -> TRefCountPtr < Task<decltype(function(T{})) >>
	{
		BaseTask* pre_req[] = { this };
		using ResultType = decltype(function(T{}));
		auto lambda = [source = TRefCountPtr<Task<T>>(this), function = std::move(function)]() -> ResultType
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
		return TaskSystem::InitializeTask(std::move(lambda), pre_req, debug_name);
	}

	template<typename F>
	auto ThenConsume(F function, const char* debug_name = nullptr) -> TRefCountPtr < Task<decltype(function(T{})) >>
	{
		BaseTask* pre_req[] = { this };
		using ResultType = decltype(function(T{}));
		auto lambda = [source = TRefCountPtr<Task<T>>(this), function = std::move(function)]() -> ResultType
			{
				if constexpr (std::is_void_v<ResultType>)
				{
					function(source->DropResult());
					return;
				}
				else
				{
					return function(source->DropResult());
				}
			};
		return TaskSystem::InitializeTask(std::move(lambda), pre_req, debug_name);
	}
};

template<>
class Task<void> : public BaseTask
{

};

class TaskSystem
{
public:
	static void StartWorkerThreads();

	static void StopWorkerThreads();

	static void WaitForWorkerThreadsToJoin();

	static bool ExecuteATask();

	template<typename F>
	static auto InitializeTask(F function, std::span<BaseTask*> prerequiers = {}, const char* debug_name = nullptr)
		-> TRefCountPtr<Task<decltype(function())>>
	{
		using ResultType = decltype(function());
		static_assert(sizeof(Task<ResultType>) == sizeof(BaseTask));
		if constexpr (std::is_void_v<ResultType>)
		{
			return InitializeTaskInner([function = std::move(function)](BaseTask&)
				{
					function();
				}, prerequiers, debug_name).Cast<Task<ResultType>>();
		}
		else
		{
			return InitializeTaskInner([function = std::move(function)](BaseTask& task)
				{
					task.result_.Store(function());
				}, prerequiers, debug_name).Cast<Task<ResultType>>();
		}
	}
#pragma region private
private:
	static TRefCountPtr<BaseTask> InitializeTaskInner(std::function<void(BaseTask&)> function, std::span<BaseTask*> prerequiers = {}, const char* debug_name = nullptr);

	static void OnReadyToExecute(BaseTask&);

	friend class BaseTask;
#pragma endregion
};

