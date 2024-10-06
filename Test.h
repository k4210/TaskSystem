#pragma once

#include <thread>
#include <functional>
#include "Profiling.h"
#include "Test.h"

namespace ts
{
struct TestDetails
{
	uint32 inner_num = 256;

	uint32 outer_num = 128;

	uint32 num_per_body = 1; // Only for stats. How many times the operation is executed by body.

	const char* name = nullptr;

	std::function<void()> excluded_initialization;

	std::function<void()> included_initialization;

	std::function<void()> included_cleanup;

	std::function<void()> excluded_cleanup;
};

template <typename TestBody>
void PerformTest(TestBody body, TestDetails test)
{
	Reporter reporter(test.name);
	for (uint32 outer_idx = 0; outer_idx < test.outer_num; outer_idx++)
	{
		if (test.excluded_initialization)
		{
			test.excluded_initialization();
		}

		reporter.start();

		if (test.included_initialization)
		{
			test.included_initialization();
		}

		for (uint32 idx = 0; idx < test.inner_num; idx++)
		{
			body(idx);
		}

		if (test.included_cleanup)
		{
			test.included_cleanup();
		}

		reporter.stop(test.inner_num * test.num_per_body);

		if (test.excluded_cleanup)
		{
			test.excluded_cleanup();
		}
	}
	reporter.display();
}
}
