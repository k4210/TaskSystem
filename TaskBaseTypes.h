#pragma once

#include "RefCount.h"
#include "RefCountPoolPtr.h"
#include "LockFree.h"
#include "Pool.h"
#include "AnyValue.h"
#include <functional>

struct DependencyNode;
class BaseTask;
class TaskSystem;
template<typename T> class Task;

enum class ETaskFlags : uint8
{
	None = 0,
	TryExecuteImmediate = 1
};

enum class ETaskState : uint8
{
#if !defined(NDEBUG)
	Nonexistent_Pooled,
#endif
	PendingOrExecuting,
	Done,
	DoneUnconsumedResult,
};

class Gate
{
public:
	Gate()
		: depending_(
#ifdef NDEBUG
			ETaskState::Done
#else
			ETaskState::Nonexistent_Pooled
#endif
		)
	{}

	~Gate()
	{
		assert(IsEmpty());
	}

	ETaskState GetState() const
	{
		return depending_.GetGateState();
	}

	bool IsEmpty() const
	{
		return depending_.IsEmpty();
	}

	// returns previous state
	ETaskState ResetStateOnEmpty(ETaskState new_state)
	{
		return depending_.SetFastOnEmpty(new_state);
	}

	void Done(ETaskState new_state);

	bool TryAddDependency(BaseTask& task);

	bool AddDependencyInner(DependencyNode& node, ETaskState required_state);

	template<typename F>
	auto Then(F function, const char* debug_name = nullptr) -> TRefCountPtr<Task<decltype(function())>>
	{
		Gate* pre_req[] = { this };
		return TaskSystem::InitializeTask(std::move(function), pre_req, debug_name);
	}

protected:
	LockFree::Collection<DependencyNode, ETaskState> depending_;
};

class CommonBase
{
public:
	bool IsPendingOrExecuting() const
	{
		const ETaskState state = gate_.GetState();
		assert(state != ETaskState::Nonexistent_Pooled);
		return state == ETaskState::PendingOrExecuting;
	}

	template<typename F>
	auto Then(F function, const char* debug_name = nullptr) -> TRefCountPtr<Task<decltype(function())>>
	{
		return gate_.Then(std::move(function), debug_name);
	}

	Gate* GetGate()
	{
		return &gate_;
	}

protected:

	void OnDestroy()
	{
		const ETaskState state = gate_.GetState();
		assert(state != ETaskState::Nonexistent_Pooled);
		if (state == ETaskState::DoneUnconsumedResult)
		{
			// RefCount is zero, so no other thread has access to the task
			result_.Reset();
			gate_.ResetStateOnEmpty(ETaskState::Done);
		}
		else
		{
			assert(state == ETaskState::Done);
		}
		assert(gate_.IsEmpty());
	}

#if !defined(NDEBUG)
	void OnReturnToPool()
	{
		const ETaskState old_state = gate_.ResetStateOnEmpty(ETaskState::Nonexistent_Pooled);
		assert(old_state != ETaskState::Nonexistent_Pooled);
	}
#endif

	LockFree::Index next_ = LockFree::kInvalidIndex;
	Gate gate_;
	AnyValue<6 * sizeof(uint8*)> result_;

	template<typename Node> friend struct LockFree::Stack;
	template<typename Node, std::size_t Size> friend struct Pool;
	template<typename T, typename DerivedType> friend class CommonSpecialization;
};

class GenericFuture : public TRefCounted<GenericFuture>, public CommonBase
{
public:
	static std::span<GenericFuture> GetPoolSpan();

protected:
	void OnRefCountZero();
};

class BaseTask : public TRefCounted<BaseTask>, public CommonBase
{
public:
	static std::span<BaseTask> GetPoolSpan();

	uint16 GetNumberOfPendingPrerequires() const;

#pragma region protected
protected:

	void Execute();

	void OnPrerequireDone();

	void OnRefCountZero();

	std::atomic<uint16> prerequires_ = 0;

	std::function<void(BaseTask&)> function_;
	const char* debug_name_ = nullptr;

	friend TRefCounted<BaseTask>;
	friend class TaskSystem;
	friend Gate;
#pragma endregion
};