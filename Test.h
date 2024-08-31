#pragma once

#include <thread>
#include <functional>
#include "Profiling.h"
#include "Test.h"

template <typename TestBody>
void PerformTest(TestBody body, uint32 inner_num, uint32 outer_num, uint32 num_per_body, const char* name,
	bool wait_for_threads_to_join = false, std::function<void()> cleanup = {})
{
	Reporter reporter(name);
	for (uint32 outer_idx = 0; outer_idx < outer_num; outer_idx++)
	{
		TaskSystem::ResetGlobals();
		TaskSystem::StartWorkerThreads();

		std::this_thread::sleep_for(4ms); // let worker threads start

		reporter.start();
		for (uint32 idx = 0; idx < inner_num; idx++)
		{
			body(idx);
		}
		if (!wait_for_threads_to_join)
		{
			reporter.stop(inner_num * num_per_body);
		}

		TaskSystem::StopWorkerThreads();
		TaskSystem::WaitForWorkerThreadsToJoin();

		if (wait_for_threads_to_join)
		{
			reporter.stop(inner_num * num_per_body);
		}

		if (cleanup)
		{
			cleanup();
		}
	}
	reporter.display();
}
