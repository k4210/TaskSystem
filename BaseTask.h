#pragma once

#include <functional>
#include <array>
#include <span>
#include "RefCount.h"
#include "RefCountPoolPtr.h"
#include "LockFree.h"
#include "Pool.h"
#include "AnyValue.h"

enum class ETaskState : uint8;
struct DependencyNode;

class BaseTask : public TRefCounted<BaseTask>
{
public:
	BaseTask();

	static std::span<BaseTask> GetPoolSpan();

	bool IsDone() const;

	void Wait();

	TRefCountPtr<BaseTask> Then(std::function<void()> function, const char* debug_name = nullptr);
#pragma region protected
protected:

	void Execute();
#if !defined(NDEBUG)
	void OnReturnToPool();
#endif
	void OnRefCountZero();

	void OnPrerequireDone();

	std::atomic_uint16_t prerequires_ = 0;
	LockFree::Index next_ = LockFree::kInvalidIndex;
	std::function<void()> function_;
	AnyValue<2 * sizeof(uint8*)> result_;
	const char* debug_name_ = nullptr;
	LockFree::Collection<DependencyNode, ETaskState> depending_;

	friend LockFree::Stack<BaseTask>;
	template<typename Node, std::size_t Size> friend struct Pool;
	friend TRefCounted<BaseTask>;
	friend class TaskSystem;
#pragma endregion
};

class TaskSystem
{
public:
	static void StartWorkerThreads();

	static void StopWorkerThreads();

	static void WaitForWorkerThreadsToJoin();

	static bool ExecuteATask();

	static TRefCountPtr<BaseTask> InitializeTask(std::function<void()> function, std::span<BaseTask*> prerequiers = {}, const char* debug_name = nullptr);
#pragma region private
private:
	static void OnReadyToExecute(BaseTask&);

	friend class BaseTask;
#pragma endregion
};