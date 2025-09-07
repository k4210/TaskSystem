#include "Coroutine.h"
#include "Task.h"
#include "Test.h"
#include "AccessSynchronizer.h"
#include "TickSync.h"
#include <array>
#include <chrono>
#include <iostream>
#include <vector>
#include "InplaceString.h"

#if TEST_MAIN

#define TASK_TEST 1
#define COROUTINE_TEST 1
#define NAMED_THREAD_TEST 1
#define SYNCH_TEST 1
#define TICK_TEST 1
#define GENERATOR_TEST 0

using namespace std::chrono_literals;

std::atomic_int32_t counter = 0;

using namespace ts;

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

class SampleAsset : public TRefCounted<SampleAsset>
{
public:
	bool locked_ = false;
	int32 data_ = 0;
	struct DebugData
	{
		int32 thread_idx = 0;
		int32 task_idx = 0;
	};
	std::vector<DebugData> debug_data_;

	std::atomic_uint32_t counter_;

	InplaceString<> str;

	AccessSynchronizer synchronizer_;

	void MutableFuntion()
	{
		counter_.fetch_sub(1, std::memory_order_relaxed);
	}

	void ConstFunction() const
	{
		const_cast<SampleAsset*>(this)->counter_.fetch_add(1, std::memory_order_relaxed);
	}

	void SaveState()
	{
		debug_data_.push_back(DebugData{ 
			t_worker_thread_idx, 
			static_cast<int32>(std::distance(BaseTask::GetPoolSpan().data(), BaseTask::GetCurrentTask()))
		});
	}
};

std::atomic<uint64> global_counter = 0;

int main()
{
	std::cout << "sizeof(AccessSynchronizer::State) : " << sizeof(AccessSynchronizer::State) << std::endl;
	std::cout << "sizeof(GenericFuture) : " << sizeof(GenericFuture) << std::endl;
	std::cout << "sizeof(Gate) : " << sizeof(Gate) << std::endl;

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
			CoroutineTest(1).StartAndDetach();
		}, TestDetails
		{
			.name = "Coroutines complex",
			.included_cleanup = WaitForTasks,
		});
	detail::ensure_allocator_free();

	PerformTest([](uint32)
		{
			TaskSystem::AsyncResume(CoroutineTest(1));
		}, TestDetails
		{
			.name = "Coroutines complex async",
			.included_cleanup = WaitForTasks,
		});
		detail::ensure_allocator_free();

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
		detail::ensure_allocator_free();
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
#if SYNCH_TEST
	{
		TRefCountPtr<SampleAsset> asset_ptr(new SampleAsset{});
		TRefCountPtr<SampleAsset> asset2_ptr(new SampleAsset{});
		SyncHolder<SampleAsset> asset(asset_ptr.Get());
		SyncHolder<SampleAsset> asset2(asset2_ptr.Get());
		
		PerformTest([&](uint32)
			{
				TaskSystem::InitializeTaskOn([&](AccessScope<SampleAsset> sample)
					{
						assert(!sample->locked_);
						sample->locked_ = true;
						sample->data_++;
						sample->locked_ = false;
					},
					asset, ETaskFlags:: TryExecuteImmediate);
			}, TestDetails
			{
				.name = "synchronizer test",
				.included_cleanup = WaitForTasks
			});
			assert(static_cast<uint32>(asset_ptr->data_) == TestDetails{}.inner_num * TestDetails{}.outer_num);
		PerformTest([&](uint32)
			{
				TaskSystem::AsyncResume([](SyncHolder<SampleAsset> in_asset, SyncHolder<SampleAsset> in_asset2) -> TDetachCoroutine
					{
						{
							AccessScopeCo<SampleAsset> guard = co_await in_asset;
							guard->SaveState();
						}
						{
							AccessScopeCo<SampleAsset> guard2 = co_await in_asset2;
							guard2->SaveState();
						}
					}(asset, asset2));
			}, TestDetails
			{
				.inner_num = 512,
				.name = "Coroutines guarded res",
				.included_cleanup = WaitForTasks,
				.excluded_cleanup = [&]()
				{
					asset_ptr->debug_data_.clear();
				 	asset2_ptr->debug_data_.clear();
				}
			});

			PerformTest([&](uint32)
			{
				TaskSystem::AsyncResume([](SyncHolder<SampleAsset> in_asset, SyncHolder<SampleAsset> in_asset2) -> TDetachCoroutine
					{
						{
							AccessScopeCo<SampleAsset> guard = co_await in_asset;
							guard->MutableFuntion();
						}
						{
							AccessScopeCo<SampleAsset> guard2 = co_await in_asset2;
							guard2->MutableFuntion();
						}
						{
							SharedAccessScopeCo<SampleAsset> guard3 = co_await in_asset.Shared();
							guard3->ConstFunction();
						}

					}(asset, asset2));
			}, TestDetails
			{
				.inner_num = 512,
				.name = "Coroutines guarded res",
				.included_cleanup = WaitForTasks,
				.excluded_cleanup = [&]()
				{
					asset_ptr->debug_data_.clear();
				 	asset2_ptr->debug_data_.clear();
				}
			});
	}
#endif
#if TICK_TEST
	{
		TickSync tick_sync;
		tick_sync.Initialize({});
		PerformTest([&](uint32)
			{
				TaskSystem::AsyncResume([](TickSync& tick_sync) -> TDetachCoroutine
					{
						TickScope tick_scope(tick_sync);
						for (int32 i = 0; i < 32; i++)
						{
							global_counter++;
							co_await tick_scope.WaitForNextFrame();
						}
					}(tick_sync));
			}, TestDetails
			{
				.inner_num = 512,
				.name = "TickSync",
				.included_cleanup = WaitForTasks,
			});
	}
#endif
	TaskSystem::StopWorkerThreadsNoWait();
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
#endif
	detail::ensure_allocator_free();
	return 0;
}

#endif // TEST_MAIN