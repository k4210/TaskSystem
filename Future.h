#pragma once

#include "Gate.h"
#include "RefCount.h"
#include "Pool.h"
#include "AnyValue.h"
#include <functional>

class TaskSystem;
template<typename T> class Future;

enum class ETaskFlags : uint8
{
	None = 0,
	TryExecuteImmediate = 1,
	DontExecute = 2,
	RedirectExecutrionForGuardedResource = 4,

	NamedThread5 = 8,
	NamedThread4 = 16,
	NamedThread3 = 32,
	NamedThread2 = 64,
	NamedThread1 = 128,

	NameThreadMask = NamedThread1 | NamedThread2 | NamedThread3 | NamedThread4 | NamedThread5
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
		using ResultType = decltype(function());
		static_assert(sizeof(Future<ResultType>) == sizeof(GenericFuture));
		Gate* pre_req[] = { &gate_ };
		return TaskSystem::InitializeTask(std::forward<F>(function), pre_req, flags LOCATION_PASS).Cast<Future<ResultType>>();
	}

	Gate* GetGate()
	{
		return &gate_;
	}

	void OnRefCountZero();
	static std::span<GenericFuture> GetPoolSpan();

	LockFree::Index next_ = LockFree::kInvalidIndex;
protected:
#if !defined(NDEBUG)
	void OnReturnToPool()
	{
		const ETaskState old_state = gate_.ResetStateOnEmpty(ETaskState::Nonexistent_Pooled);
		assert(old_state != ETaskState::Nonexistent_Pooled);
	}
#endif

	Gate gate_;
	AnyValue<6 * sizeof(uint8*)> result_;
	
	friend class TaskSystem;
	template<typename T, typename DerivedType> friend class CommonSpecialization;
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
