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

struct DependencyNode;

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

	void Wait();

	uint16 GetNumberOfPendingPrerequires() const
	{
		return prerequires_.load(std::memory_order_relaxed);
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
	std::function<void(BaseTask&)> function_;
	AnyValue<4 * sizeof(uint8*)> result_;
	const char* debug_name_ = nullptr;
	LockFree::Collection<DependencyNode, ETaskState> depending_;

	friend LockFree::Stack<BaseTask>;
	template<typename Node, std::size_t Size> friend struct Pool;
	friend TRefCounted<BaseTask>;
	friend class TaskSystem;
#pragma endregion
};

template<typename T>
class Task : public BaseTask
{
public:
	T DropResultUnsafe() //Not thread safe, must be called once, on a single thread
	{
		const ETaskState old_state = depending_.SetFastOnEmpty(ETaskState::Done);
		assert(old_state == ETaskState::DoneUnconsumedResult);
		return result_.GetOnce();
	}

	std::optional<T> DropResult()
	{
		const ETaskState old_state = depending_.SetFastOnEmpty(ETaskState::Done);
		assert(old_state == ETaskState::DoneUnconsumedResult || old_state == ETaskState::Done);
		if (old_state == ETaskState::DoneUnconsumedResult)
		{
			return result_.GetOnce();
		}
		return {};
	}

	template<typename F>
	auto Then(F function, const char* debug_name = nullptr) -> TRefCountPtr<Task<decltype(function())>>
	{
		BaseTask* pre_req[] = { this };
		return TaskSystem::InitializeTask(std::move(function), pre_req, debug_name);
	}
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
			return TRefCountPtr<Task<ResultType>>(InitializeTaskInner([function = std::move(function)](BaseTask&)
				{
					function();
				}, prerequiers, debug_name));
		}
		else
		{
			return TRefCountPtr<Task<ResultType>>(InitializeTaskInner([function = std::move(function)](BaseTask& task)
				{
					task.result_.Store(function());
				}, prerequiers, debug_name));
		}
	}
#pragma region private
private:
	static TRefCountPtr<BaseTask> InitializeTaskInner(std::function<void(BaseTask&)> function, std::span<BaseTask*> prerequiers = {}, const char* debug_name = nullptr);

	static void OnReadyToExecute(BaseTask&);

	friend class BaseTask;
#pragma endregion
};

