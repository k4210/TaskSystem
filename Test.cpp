#include <iostream>
#include <sstream>
#include <chrono>
#include <thread>
#include <vector>
#include <memory>
#include <future>
#include <array>
#include "Task.h"
#include "Coroutine.h"
#include "Test.h"

using namespace std::chrono_literals;

std::atomic_int32_t counter = 0;

TUniqueCoroutine<int32> InnerCoroutine()
{
	int32 val = co_await TaskSystem::InitializeTask([]() -> int32
		{
			counter.fetch_add(1, std::memory_order_relaxed);
			return 3;
		}, {}, "task in inner coroutine");
	co_return val;
}

TUniqueCoroutine<int32> CoroutineTest()
{
	TRefCountPtr<Task<int32>> async_task = TaskSystem::InitializeTask([]() -> int32
		{
			counter.fetch_add(1, std::memory_order_relaxed);
			return 1;
		}, {}, "task 1 in coroutine");

	int32 val2 = co_await TaskSystem::InitializeTask([]() -> int32
		{
			counter.fetch_add(1, std::memory_order_relaxed);
			return 2;
		}, {}, "task 2 in coroutine");

	int32 val1 = co_await std::move(async_task);

	int32 val3 = co_await InnerCoroutine();

	co_return val1 + val2 + val3;
};

int main()
{
	auto LambdaEmpty = []()
		{
			counter.fetch_add(1, std::memory_order_relaxed);
		};

	auto LambdaRead = [](const std::string&) -> void
		{
			counter.fetch_add(1, std::memory_order_relaxed);
		};

	auto LambdaConsume = [](std::string) -> void
		{
			counter.fetch_add(1, std::memory_order_relaxed);
		};

	auto StartTaskSystem = []()
		{
			//TaskSystem::ResetGlobals();
			TaskSystem::StartWorkerThreads();

			std::this_thread::sleep_for(4ms); // let worker threads start
		};

	auto StopTaskSystem = []()
		{
			TaskSystem::StopWorkerThreads();
			TaskSystem::WaitForWorkerThreadsToJoin();
		};

	PerformTest([&](uint32)
		{
			TRefCountPtr<Task<>> A = TaskSystem::InitializeTask(LambdaEmpty, {}, "a");
			A->Then(LambdaEmpty, "b");
			A->Then(LambdaEmpty, "c");
			A->Then(LambdaEmpty, "d");
		}, TestDetails
		{
			.num_per_body = 4,
			.name = "Then test",
			.excluded_initialization = StartTaskSystem,
			.excluded_cleanup = StopTaskSystem
		});

	PerformTest([&](uint32)
		{
			TRefCountPtr<Task<>> A = TaskSystem::InitializeTask(LambdaEmpty, {}, "a");
			TRefCountPtr<Task<>> B = TaskSystem::InitializeTask(LambdaEmpty, {}, "b");
			TRefCountPtr<Task<>> C = TaskSystem::InitializeTask(LambdaEmpty, {}, "c");

			BaseTask* Arr[]{ A, B, C };
			TaskSystem::InitializeTask(LambdaEmpty, Arr, "d");
		}, TestDetails
		{
			.num_per_body = 4,
			.name = "InitializeTask test",
			.excluded_initialization = StartTaskSystem,
			.excluded_cleanup = StopTaskSystem
		});

	PerformTest([&](uint32)
		{
			TRefCountPtr<Task<>> A = TaskSystem::InitializeTask(LambdaEmpty, {}, "a");
			TRefCountPtr<Task<>> B = TaskSystem::InitializeTask(LambdaEmpty, {}, "b");
			TRefCountPtr<Task<>> C = TaskSystem::InitializeTask(LambdaEmpty, {}, "c");

			BaseTask* Arr[]{ A, B, C };
			TaskSystem::InitializeTask(LambdaEmpty, Arr, "d");
		}, TestDetails
		{
			.num_per_body = 4,
			.name = "Execute empty test",
			.excluded_initialization = StartTaskSystem,
			.included_cleanup = StopTaskSystem
		});

	std::array<TUniqueCoroutine<int32>, 1024> handles;
	PerformTest([&handles](uint32 idx)
		{
			handles[idx] = CoroutineTest();
		}, TestDetails
		{
			.inner_num = 1024,
			.name = "Coroutines complex",
			.excluded_initialization = StartTaskSystem,
			.included_cleanup = StopTaskSystem,
			.excluded_cleanup = [&handles]()
			{
				for (auto& handle : handles)
				{
					handle = {};
				}
			}
		});

	return 0;
}