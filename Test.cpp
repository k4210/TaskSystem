#include "Coroutine.h"
#include "Task.h"
#include "Test.h"
#include <array>
#include <chrono>
#include <iostream>

using namespace std::chrono_literals;

std::atomic_int32_t counter = 0;

TDetachCoroutine CoroutineTest(int32 in_val)
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

	assert(in_val && val1 && val2 && val3);
	//co_return val1 + val2 + val3;
};

int main()
{
	TaskSystem::StartWorkerThreads();
	std::this_thread::sleep_for(4ms); // let worker threads start

	auto WaitForTasks = []
		{
			TaskSystem::WaitForAllTasks();
		};

	auto LambdaProduce = []() -> std::string
		{
			counter.fetch_add(1, std::memory_order_relaxed);
			return "yada hey";
		};
#if 1
	auto LambdaEmpty = []()
		{
			counter.fetch_add(1, std::memory_order_relaxed);
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
#endif
	PerformTest([](uint32)
		{
			CoroutineTest(1).ResumeAndDetach();
		}, TestDetails
		{
			.name = "Coroutines complex",
			.included_cleanup = WaitForTasks,
		});
	Coroutine::detail::ensure_allocator_free();

	PerformTest([](uint32)
		{
			TaskSystem::AsyncResume(CoroutineTest(1));
		}, TestDetails
		{
			.name = "Coroutines complex async",
			.included_cleanup = WaitForTasks,
		});
		Coroutine::detail::ensure_allocator_free();

	{
		std::array<TUniqueCoroutine<int32>, 1024> handles;
		auto ResetHandles = [&handles]
			{
				for (auto& handle : handles)
				{
					handle = {};
				}
			};

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
		Coroutine::detail::ensure_allocator_free();
	}
#if 1 // Named thread test
	{
		bool working = true;
		auto body = [&working](ETaskFlags flag, std::atomic<bool>* is_active)
			{
				while (working)
				{
					if (!TaskSystem::ExecuteATask(flag, *is_active))
					{
						std::this_thread::yield();
					}
				}
			};
		std::atomic<bool> active0 = true;
		std::atomic<bool> active1 = true;
		std::thread named0(body, ETaskFlags::NamedThread0, &active0);
		std::thread named1(body, ETaskFlags::NamedThread1, &active1);
		TaskSystem::InitializeTask([]{ std::cout << "Yada0\n"; }, {}, ETaskFlags::NamedThread0);
		TaskSystem::InitializeTask([]{ std::cout << "Yada1\n"; }, {}, ETaskFlags::NamedThread1);
		while (active0 || active1) { std::this_thread::yield(); }
		working = false;
		named0.join();
		named1.join();
	}
#endif
	TaskSystem::StopWorkerThreads();
	TaskSystem::WaitForWorkerThreadsToJoin();

#if 1
	{
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
	}
	Coroutine::detail::ensure_allocator_free();
#endif
	return 0;
}