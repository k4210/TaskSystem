#pragma once

#include <chrono>
#include <iostream>
#include <array>
#include "Common.h"

namespace ts
{
	using TimeSpan = std::chrono::nanoseconds;
	inline auto GetTime() { return std::chrono::high_resolution_clock::now(); }
	using TimeType = decltype(GetTime());
	inline double ToMiliseconds(TimeSpan duration)
	{
		return duration.count() / (1000.0 * 1000.0);
	}

	struct AvgTime
	{
		//static constexpr uint32 windows_size = 128;

		//std::array<float, windows_size> window = {0.0f};
		double duration_sum_ms = 0.0;
		uint32 execution_num = 0;
		uint32 frames_to_skip = 320;

		//Returns current time
		TimeType add(const TimeType scope_begin)
		{
			const TimeType current_time = GetTime();
			if(frames_to_skip)
			{ 			
				frames_to_skip--;
				return current_time;
			}
			duration_sum_ms += ToMiliseconds(current_time - scope_begin);

			//window[execution_num % windows_size] = duration_ms;
			execution_num++;
			return current_time;
		}

		float average() const
		{
			return (execution_num > 0)
				? static_cast<float>(duration_sum_ms / execution_num) 
				: 0.0f;
		}
		/*
		float average_window() const
		{
			if (execution_num < windows_size)
			{
				return 0.0f;
			}

			double sum = 0.0f;
			for (uint32 idx = 0; idx < windows_size; idx++)
			{
				sum += window[idx];
			}
			return static_cast<float>(sum / windows_size);
		}
		*/
	};

	class Reporter
	{
		TimeType start_time_;
		const char* name_;
		double duration_sum_ms = 0.0;
		uint32 execution_num = 0;
	public:
		Reporter(const char* in_name)
			: name_(in_name)
		{
			std::cout << std::endl << "Start test " << name_ << std::endl;
		}

		~Reporter()
		{
			std::cout << "Stop test " << name_ << std::endl;
		}

		void start()
		{
			start_time_ = GetTime();
		}

		void stop(uint32 num)
		{
			const TimeType last_time = GetTime();
			const TimeSpan duration = last_time - start_time_;
			const double duration_ms = ToMiliseconds(duration);

			duration_sum_ms += duration_ms;
			execution_num += num;

			std::cout << ".";
		}

		void display()
		{
			const double body_avg_ms = duration_sum_ms / execution_num;
			std::cout << std::endl << "Executed: "
				<< execution_num << ", duration: "
				<< duration_sum_ms << "ms. Average body time: "
				<< body_avg_ms << "ms." << std::endl;
		};
	};

}

