#include "BaseTask.h"
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
			TRefCountPtr<BaseTask> A = TaskSystem::InitializeTask({ Lambda }, {}, "a")->Then({ Lambda }, "aa");
			TRefCountPtr<BaseTask> B = TaskSystem::InitializeTask({ Lambda }, {}, "b");
			B->Then({ Lambda }, "bb");
			TRefCountPtr<BaseTask> C = TaskSystem::InitializeTask({ Lambda }, {}, "c");
			BaseTask* Arr[]{ A, B, C};
			TaskSystem::InitializeTask({ Lambda }, Arr, "d");
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
	return 0;
}