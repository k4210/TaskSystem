#pragma once

#include "Future.h"
#include <functional>

namespace ts
{
	class BaseTask : public GenericFuture
	{
	public:
		static std::span<BaseTask> GetPoolSpan();
		static BaseTask* GetCurrentTask();

		static void OnUnblocked(utils::TRefCountPtr<BaseTask> task, utils::TRefCountPtr<BaseTask>* out_first_ready_dependency);
		void Execute(utils::TRefCountPtr<BaseTask>* out_first_ready_dependency = nullptr);

		ETaskFlags GetFlags() const { return flag_; }
#pragma region protected
	protected:
		friend class TaskSystem;
		friend class GenericFuture;
		friend struct AccessSynchronizer;

		std::atomic<uint16> prerequires_ = 0;
		ETaskFlags flag_ = ETaskFlags::None;

		std::move_only_function<void(BaseTask&)> function_;
		DEBUG_CODE(std::source_location source;)
#pragma endregion
	};
}