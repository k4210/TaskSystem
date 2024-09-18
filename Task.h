#pragma once

#include "BaseTask.h"
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

	uint16 GetNumberOfPendingPrerequires() const;

#pragma region protected
protected:

	void Execute(TRefCountPtr<BaseTask>* out_first_ready_dependency = nullptr);

	void OnPrerequireDone(TRefCountPtr<BaseTask>* out_first_ready_dependency);

	std::atomic<uint16> prerequires_ = 0;
	ETaskFlags flag_ = ETaskFlags::None;

	std::function<void(BaseTask&)> function_;
	DEBUG_CODE(std::source_location source;)

	friend TRefCounted<BaseTask>;
	friend class TaskSystem;
	friend Gate;
	friend GuardedResourceBase;
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

struct DependencyNode
{
	static std::span<DependencyNode> GetPoolSpan();
#if !defined(NDEBUG)
	void OnReturnToPool()
	{
		assert(!task_);
	}
#endif
	TRefCountPoolPtr<BaseTask> task_;

	LockFree::Index next_ = LockFree::kInvalidIndex;
};

struct TaskSystemGlobals
{
	Pool<BaseTask, 1024> task_pool_;
	Pool<DependencyNode, 4096> dependency_pool_;
	Pool<GenericFuture, 1024> future_pool_;
	LockFree::Stack<BaseTask> ready_to_execute_;
	LockFree::Stack<BaseTask> ready_to_execute_named0;
	LockFree::Stack<BaseTask> ready_to_execute_named1;

	std::array<std::thread, 16> threads_;
	bool working_ = false;
	std::atomic<uint8> used_threads_ = 0;

	LockFree::Stack<BaseTask>& ReadyStack(ETaskFlags flag)
	{
		if (enum_has_any(flag, ETaskFlags::NamedThread0))
		{
			return ready_to_execute_named0;
		}
		else if (enum_has_any(flag, ETaskFlags::NamedThread1))
		{
			return ready_to_execute_named1;
		}
		return ready_to_execute_;
	}
};

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

	static TaskSystemGlobals& GetGlobals();

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
