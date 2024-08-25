#include "Task.h"
#include <iostream>
#include <sstream>
#include <chrono>
#include <thread>
#include <vector>
#include <memory>
#include <future>

using TimeSpan = std::chrono::nanoseconds;
inline auto GetTime() { return std::chrono::high_resolution_clock::now(); }
using TimeType = decltype(GetTime());
inline double ToMiliseconds(TimeSpan duration)
{
	return duration.count() / (1000.0 * 1000.0);
}

struct Reporter
{
	std::vector<std::string> raports_;
	TimeType start_time;
	TimeType last_time;

	Reporter()
	{
		raports_.reserve(64);
		start_time = GetTime();
		last_time = start_time;
	}

	void wait(TimeSpan span)
	{
		TimeType current_time;
		do
		{
			current_time = GetTime();
		} while ((current_time - last_time) < span);
		last_time = current_time;
	}

	void raport(int32 counter, const char* add_msg = nullptr)
	{
		const TimeSpan duration = last_time - start_time;
		std::ostringstream buf;
		buf << "Executed: " << counter << ", duration: " << ToMiliseconds(duration) << "ms. ";
		if (add_msg)
		{
			buf << add_msg;
		}
		raports_.push_back(buf.str());
	};

	void display()
	{
		for (const std::string& str : raports_)
		{
			std::cout << str << std::endl;
		}
	}
};

int main()
{
	using namespace std::chrono_literals;

	std::cout << "sizeof(std::function<void()>) " << sizeof(std::function<void()>) << std::endl;
	std::cout << "sizeof(std::packaged_task<void()>) " << sizeof(std::packaged_task<void()>) << std::endl;
	std::cout << "sizeof(BaseTask) " << sizeof(BaseTask) << std::endl;
	std::cout << "sizeof(std::string) " << sizeof(std::string) << std::endl;
	std::cout << "alignof(void*) " << alignof(void*) << std::endl;
	std::cout << "alignof(std::string) " << alignof(std::string) << std::endl;
	/*
	for (int32 Pass = 0; Pass < 50; Pass++)
	{
		std::cout << "StartWorkerThreads" << std::endl;
		TaskSystem::StartWorkerThreads();
		std::this_thread::sleep_for(4ms); // let worker threads start

		std::atomic_int32_t counter = 0;
		auto Lambda = [&counter]()
			{
				//std::this_thread::sleep_for(0us);
				counter.fetch_add(1, std::memory_order_relaxed);
			};

		Reporter reporter;

		for (int32 Idx = 0; Idx < 256; Idx++)
		//while((GetTime() - reporter.start_time) < 1ms)
		{
			TRefCountPtr<Task<void>> A = TaskSystem::InitializeTask(Lambda, {}, "a")->Then(Lambda, "aa");
			TRefCountPtr<Task<void>> B = TaskSystem::InitializeTask(Lambda, {}, "b");
			BaseTask* Arr[]{ A, B };
			TaskSystem::InitializeTask(Lambda, Arr, "c");
		}

		reporter.wait(0us);
		reporter.raport(counter, "task initialization complete");
		do
		{
			reporter.wait(50us);
			reporter.raport(counter);
		} while (counter < 1000);

		TaskSystem::StopWorkerThreads();
		reporter.display();
		std::cout << "StopWorkerThreads" << std::endl;

		TaskSystem::WaitForWorkerThreadsToJoin();
	}
	*/

	for (int32 Pass = 0; Pass < 50; Pass++)
	{
		std::cout << "StartWorkerThreads" << std::endl;
		TaskSystem::StartWorkerThreads();
		std::this_thread::sleep_for(4ms); // let worker threads start

		std::atomic_int32_t counter = 0;
		auto Lambda = [&counter]() -> std::string
			{
				counter.fetch_add(1, std::memory_order_relaxed);
				return "test";
			};

		auto LambdaRead = [&counter](const std::string& str) -> void
			{
				counter.fetch_add(1, std::memory_order_relaxed);
				std::cout << str << std::endl;
			};

		auto LambdaConsume = [&counter](std::string str) -> void
			{
				counter.fetch_add(1, std::memory_order_relaxed);
				std::cout << str << std::endl;
			};

		Reporter reporter;

		for (int32 Idx = 0; Idx < 256; Idx++)
			//while((GetTime() - reporter.start_time) < 1ms)
		{
			TRefCountPtr<Task<std::string>> A = TaskSystem::InitializeTask(Lambda, {}, "a");
			A->ThenRead(LambdaRead, "b");
			A->ThenRead(LambdaRead, "c");
			A->ThenRead(LambdaRead, "d");
			//TRefCountPtr<Task<void>> BB = TaskSystem::InitializeTask(Lambda, {}, "b")->ThenConsume(LambdaConsume, "aa");
			//TRefCountPtr<Task<void>> CC = TaskSystem::InitializeTask(Lambda, {}, "c")->ThenRead(LambdaRead, "cc");

			//TRefCountPtr<Task<std::string>> BB = TaskSystem::InitializeTask(Lambda, {}, "c")->Then(Lambda, "cc");
			//BaseTask* Arr[]{ A, B };
			//TaskSystem::InitializeTask(Lambda, Arr, "c");
		}

		reporter.wait(0us);
		reporter.raport(counter, "task initialization complete");
		do
		{
			reporter.wait(50us);
			reporter.raport(counter);
		} while (counter < 1024);

		TaskSystem::StopWorkerThreads();
		reporter.display();
		std::cout << "StopWorkerThreads" << std::endl;

		TaskSystem::WaitForWorkerThreadsToJoin();
	}

	return 0;
}