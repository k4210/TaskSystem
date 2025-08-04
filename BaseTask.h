#pragma once

#include "Future.h"
#include <functional>

namespace ts
{
	class BaseTask : public GenericFuture
	{
	public:
		using IndexType = BaseIndex<BaseTask>;
		static std::span<BaseTask> GetPoolSpan();
		static BaseTask* GetCurrentTask();

		static void OnUnblocked(TRefCountPtr<BaseTask> task, TRefCountPtr<BaseTask>* out_first_ready_dependency);
		void Execute(TRefCountPtr<BaseTask>* out_first_ready_dependency = nullptr);

		ETaskFlags GetFlags() const { return flag_; }

		IndexType& NextRef() 
		{ 
			static_assert(sizeof(IndexType) == sizeof(Index), "IndexType must be same size as Index");
			return *reinterpret_cast<IndexType*>(&next_);
		}
#if TASK_RETRIGGER
		void SetRetrigger()
		{
			assert(!result_.HasValue());
			assert(flag_ == ETaskFlags::None);
			assert(!retrigger_);
			retrigger_ = true;
		}
#endif
#pragma region protected
	protected:
		friend class TaskSystem;
		friend class GenericFuture;
		friend struct AccessSynchronizer;

		std::atomic<uint16> prerequires_ = 0;
		ETaskFlags flag_ = ETaskFlags::None;
#if TASK_RETRIGGER
		bool retrigger_ = false;
#endif
		std::move_only_function<void(BaseTask&)> function_;
		DEBUG_CODE(std::source_location source;)
#pragma endregion
	};
}