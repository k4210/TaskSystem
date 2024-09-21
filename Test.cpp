#include "Coroutine.h"
#include "Task.h"
#include "GuardedResource.h"
#include "Test.h"
#include <array>
#include <chrono>
#include <iostream>

using namespace std::chrono_literals;

std::atomic_int32_t counter = 0;

TDetachCoroutine CoroutineTest(int32 in_val)
{
	TRefCountPtr<Future<int32>> async_task = TaskSystem::InitializeTask([]() -> int32
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

#define TASK_TEST 1
#define COROUTINE_TEST 1
#define NAMED_THREAD_TEST 1
#define GUARDED_TEST 1
#define GENERATOR_TEST 1

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
#if TASK_TEST
	PerformTest([&](uint32)
		{
			TRefCountPtr<Future<>> A = TaskSystem::InitializeTask(LambdaEmpty);
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
			TRefCountPtr<Future<>> A = TaskSystem::InitializeTask(LambdaEmpty);
			TRefCountPtr<Future<>> B = TaskSystem::InitializeTask(LambdaEmpty);
			TRefCountPtr<Future<>> C = TaskSystem::InitializeTask(LambdaEmpty);

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
			TRefCountPtr<Future<>> A = TaskSystem::InitializeTask(LambdaEmpty);
			TRefCountPtr<Future<>> B = TaskSystem::InitializeTask(LambdaEmpty);
			TRefCountPtr<Future<>> C = TaskSystem::InitializeTask(LambdaEmpty);

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
			TRefCountPtr<Future<std::string>> A = TaskSystem::InitializeTask(LambdaProduce);
			A->ThenRead(LambdaRead);
			TRefCountPtr<Future<std::string>> C = A->ThenRead(LambdaReadPass);
			C->ThenConsume(LambdaConsume);
		}, TestDetails
		{
			.num_per_body = 4,
			.name = "Read and consume test",
			.included_cleanup = WaitForTasks
		});
#endif
#if COROUTINE_TEST
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
				TRefCountPtr<Future<std::string>> task = future->Then(LambdaProduce);
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
#endif
#if NAMED_THREAD_TEST
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
		std::atomic<bool> active2 = true;
		std::atomic<bool> active1 = true;
		std::thread named2(body, ETaskFlags::NamedThread2, &active2);
		std::thread named1(body, ETaskFlags::NamedThread1, &active1);
		TaskSystem::InitializeTask([]{ std::cout << "Yada2\n"; }, {}, ETaskFlags::NamedThread2);
		TaskSystem::InitializeTask([]{ std::cout << "Yada1\n"; }, {}, ETaskFlags::NamedThread1);
		while (active2 || active1) { std::this_thread::yield(); }
		working = false;
		named2.join();
		named1.join();
	}
#endif
#if GUARDED_TEST
	{
		TRefCountPtr<GuardedResource<int32>> resource(new GuardedResource<int32>(0));
		PerformTest([&](uint32)
			{
				TaskSystem::InitializeTask([&]()
					{
						resource->UseWhenAvailible([](int32& res) { res++; });
					}
				);
			}, TestDetails
			{
				.inner_num = 512,
				.name = "guarded test",
				.included_cleanup = WaitForTasks
			});
		resource->UseWhenAvailible([](int32 res) { assert(res == 512 * 64); });
		PerformTest([&](uint32)
			{
				TaskSystem::AsyncResume([](TRefCountPtr<GuardedResource<int32>> resource) -> TDetachCoroutine
					{
						{
							ResourceAccessScope<int32> guard = co_await resource;
							guard.Get()--;
						}
					}(resource));
			}, TestDetails
			{
				.inner_num = 512,
				.name = "Coroutines guarded res",
				.included_cleanup = WaitForTasks,
			});
		resource->UseWhenAvailible([](int32 res) { assert(res == 0); });
	}
#endif
	TaskSystem::StopWorkerThreads();
	TaskSystem::WaitForWorkerThreadsToJoin();

#if GENERATOR_TEST
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