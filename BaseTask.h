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
template<typename T> class Future;

enum class ETaskFlags : uint8
{
	None = 0,
	TryExecuteImmediate = 1,
	NamedThread0 = 2,
	NamedThread1 = 4,
	DontExecute = 8,

	NameThreadMask = NamedThread0 | NamedThread1
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

	void Done(ETaskState new_state, TRefCountPtr<BaseTask>* out_first_ready_dependency = nullptr);

	bool TryAddDependency(BaseTask& task);

	bool AddDependencyInner(DependencyNode& node, ETaskState required_state);

	template<typename F>
	auto Then(F&& function, ETaskFlags flags = ETaskFlags::None LOCATION_PARAM) -> TRefCountPtr<Future<decltype(function())>>;

protected:
	LockFree::Collection<DependencyNode, ETaskState> depending_;
};

class GenericFuture : public TRefCounted<GenericFuture>
{
public:
	bool IsPendingOrExecuting() const
	{
		const ETaskState state = gate_.GetState();
		assert(state != ETaskState::Nonexistent_Pooled);
		return state == ETaskState::PendingOrExecuting;
	}

	template<typename F>
	auto Then(F function, ETaskFlags flags = ETaskFlags::None LOCATION_PARAM) -> TRefCountPtr<Future<decltype(function())>>
	{
		return gate_.Then(std::move(function), flags LOCATION_PASS);
	}

	Gate* GetGate()
	{
		return &gate_;
	}

	void OnRefCountZero();
	static std::span<GenericFuture> GetPoolSpan();

protected:
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

	friend class TaskSystem;
	template<typename Node> friend struct LockFree::Stack;
	template<typename A, typename B> friend struct LockFree::Collection;
	template<typename Node, std::size_t Size> friend struct Pool;
	template<typename T, typename DerivedType> friend class CommonSpecialization;
	friend class GuardedResourceBase;
	template<typename T> friend class GuardedResource;
};

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

template<typename F>
auto Gate::Then(F&& function, ETaskFlags flags LOCATION_PARAM_IMPL) -> TRefCountPtr<Future<decltype(function())>>
{
	using ResultType = decltype(function());
	Gate* pre_req[] = { this };
	static_assert(sizeof(Future<ResultType>) == sizeof(GenericFuture));
	return TaskSystem::InitializeTask(std::forward<F>(function), pre_req, flags LOCATION_PASS).Cast<Future<ResultType>>();
}