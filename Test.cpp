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

	constexpr uint32 outer_loop = 64;
	constexpr uint32 inner_loop = 256;
	//const uint32 num_per_body = 4;
	const bool wait_for_threads_to_join = true;
	/*
	PerformTest([&](uint32)
		{
			TRefCountPtr<Task<>> A = TaskSystem::InitializeTask(LambdaEmpty, {}, "a");
			A->Then(LambdaEmpty, "b");
			A->Then(LambdaEmpty, "c");
			A->Then(LambdaEmpty, "d");
		}, inner_loop, outer_loop, num_per_body, "A->Then");

	PerformTest([&](uint32)
		{
			TRefCountPtr<Task<>> A = TaskSystem::InitializeTask(LambdaEmpty, {}, "a");
			TRefCountPtr<Task<>> B = TaskSystem::InitializeTask(LambdaEmpty, {}, "b");
			TRefCountPtr<Task<>> C = TaskSystem::InitializeTask(LambdaEmpty, {}, "c");

			BaseTask* Arr[]{ A, B, C };
			TaskSystem::InitializeTask(LambdaEmpty, Arr, "d");
		}, inner_loop, outer_loop, num_per_body, "InitializeTask");

	PerformTest([&](uint32)
		{
			TRefCountPtr<Task<>> A = TaskSystem::InitializeTask(LambdaEmpty, {}, "a");
			TRefCountPtr<Task<>> B = TaskSystem::InitializeTask(LambdaEmpty, {}, "b");
			TRefCountPtr<Task<>> C = TaskSystem::InitializeTask(LambdaEmpty, {}, "c");

			BaseTask* Arr[]{ A, B, C };
			TaskSystem::InitializeTask(LambdaEmpty, Arr, "d");
		}, inner_loop, outer_loop, num_per_body, "Execute epmty tasks", wait_for_threads_to_join);
		*/
	std::array<TUniqueCoroutine<int32>, 4 * inner_loop> handles;
	PerformTest([&handles](uint32 idx)
		{
			handles[idx] = CoroutineTest();
		}, 4 * inner_loop, 16 * outer_loop, 1, "Coroutines complex", wait_for_threads_to_join,
		[&handles]()
		{
			for (auto& handle : handles)
			{
				handle = {};
			}
		});

	return 0;
}