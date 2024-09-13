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

TUniqueCoroutine<int32> CoroutineTest()
{
	TRefCountPtr<Task<int32>> async_task = TaskSystem::InitializeTask([]() -> int32
		{
			counter.fetch_add(1, std::memory_order_relaxed);
			return 1;
		});

	int32 val2 = co_await TaskSystem::InitializeTask([]() -> int32
		{
			counter.fetch_add(1, std::memory_order_relaxed);
			return 2;
		});

	int32 val1 = co_await std::move(async_task);

	int32 val3 = co_await []() ->TUniqueCoroutine<int32>
	{
		int32 val = co_await TaskSystem::InitializeTask([]() -> int32
			{
				counter.fetch_add(1, std::memory_order_relaxed);
				return 3;
			});
		co_return val;
	}();

	co_return val1 + val2 + val3;
};

int main()
{
	auto LambdaEmpty = []()
		{
			counter.fetch_add(1, std::memory_order_relaxed);
		};

	auto LambdaProduce = []() -> std::string
		{
			counter.fetch_add(1, std::memory_order_relaxed);
			return "yada hey";
		};

	auto LambdaRead = [](const std::string&) -> void
		{
			counter.fetch_add(1, std::memory_order_relaxed);
		};

	auto LambdaReadPass = [](const std::string& str) -> std::string
		{
			counter.fetch_add(1, std::memory_order_relaxed);
			return str;
		};

	auto LambdaConsume = [](std::string) -> void
		{
			counter.fetch_add(1, std::memory_order_relaxed);
		};

	auto WaitForTasks = []
		{
			TaskSystem::WaitForAllTasks();
		};

	TaskSystem::StartWorkerThreads();
	std::this_thread::sleep_for(4ms); // let worker threads start

	PerformTest([&](uint32)
		{
			TRefCountPtr<Task<>> A = TaskSystem::InitializeTask(LambdaEmpty);
			A->Then(LambdaEmpty);
			A->Then(LambdaEmpty);
			A->Then(LambdaEmpty);
		}, TestDetails
		{
			.num_per_body = 4,
			.name = "Then test",
			.excluded_cleanup = WaitForTasks
		});

	PerformTest([&](uint32)
		{
			TRefCountPtr<Task<>> A = TaskSystem::InitializeTask(LambdaEmpty);
			TRefCountPtr<Task<>> B = TaskSystem::InitializeTask(LambdaEmpty);
			TRefCountPtr<Task<>> C = TaskSystem::InitializeTask(LambdaEmpty);

			Gate* Arr[]{ A->GetGate(), B->GetGate(), C->GetGate() };
			TaskSystem::InitializeTask(LambdaEmpty, Arr);
		}, TestDetails
		{
			.num_per_body = 4,
			.name = "InitializeTask test",
			.excluded_cleanup = WaitForTasks
		});

	PerformTest([&](uint32)
		{
			TRefCountPtr<Task<>> A = TaskSystem::InitializeTask(LambdaEmpty);
			TRefCountPtr<Task<>> B = TaskSystem::InitializeTask(LambdaEmpty);
			TRefCountPtr<Task<>> C = TaskSystem::InitializeTask(LambdaEmpty);

			Gate* Arr[]{ A->GetGate(), B->GetGate(), C->GetGate() };
			TaskSystem::InitializeTask(LambdaEmpty, Arr);
		}, TestDetails
		{
			.num_per_body = 4,
			.name = "Execute empty test",
			.included_cleanup = WaitForTasks
		});

	PerformTest([&](uint32)
		{
			TRefCountPtr<Task<std::string>> A = TaskSystem::InitializeTask(LambdaProduce);
			A->ThenRead(LambdaRead);
			TRefCountPtr<Task<std::string>> C = A->ThenRead(LambdaReadPass);
			C->ThenConsume(LambdaConsume);
		}, TestDetails
		{
			.num_per_body = 4,
			.name = "Read and consume test",
			.included_cleanup = WaitForTasks
		});
	
	std::array<TUniqueCoroutine<int32>, 1024> handles;
	auto ResetHandles = [&handles]
		{
			for (auto& handle : handles)
			{
				handle = {};
			}
		};

	PerformTest([&handles](uint32 idx)
		{
			handles[idx] = CoroutineTest();
		}, TestDetails
		{
			.inner_num = 1024,
			.name = "Coroutines complex",
			.included_cleanup = WaitForTasks,
			.excluded_cleanup = ResetHandles
		});

	PerformTest([&](uint32 idx)
		{
			TRefCountPtr<Future<int32>> future = TaskSystem::MakeFuture<int32>();
			TRefCountPtr<Task<std::string>> task = future->Then(LambdaProduce);
			handles[idx] = [](TRefCountPtr<Future<int32>> future) -> TUniqueCoroutine<int32>
				{
					const int32 value = co_await std::move(future);
					assert(5 == value);
					co_return value;
				}(future);
			future->Done(5);
		}, TestDetails
		{
			.inner_num = 1024,
			.name = "future",
			.included_cleanup = WaitForTasks,
			.excluded_cleanup = ResetHandles
		});

	TaskSystem::StopWorkerThreads();
	TaskSystem::WaitForWorkerThreadsToJoin();

	std::cout << "Generator test:\n";
	TUniqueCoroutine<void, int32> generator = []() -> TUniqueCoroutine<void, int32>
		{
			int32 fn = 0;
			co_yield fn;
			int32 fn1 = 1;
			co_yield fn1;
			while (true)
			{
				const int32 result = fn + fn1;
				fn = fn1;
				fn1 = result;
				co_yield result;
			}
		}();
	for (int32 val = generator.ConsumeYield().value(); val < 10000; 
		generator.TryResume(), val = generator.ConsumeYield().value())
	{
		std::cout << val << " ";
	}
	generator.Destroy();
	std::cout << "\nGenerator test done\n";
	return 0;
}