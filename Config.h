#pragma once

#include <cstddef>

#define COROUTINE_CUSTOM_ALLOC 1

#define THREAD_SMART_POOL 1

#define TASK_RETRIGGER 0

#define TEST_MAIN 0

#ifdef NDEBUG
#define DO_POOL_STATS 0
#else
#define DO_POOL_STATS 1
#endif // _DEBUG

namespace ts
{
	constexpr std::size_t kWorkeThreadsNum = 32;
	constexpr std::size_t kTaskPoolSize = 1024 * 8;
	constexpr std::size_t kDepNodePoolSize = 1024 * 8;
	constexpr std::size_t kFuturePoolSize = 2048;

	constexpr std::size_t InitPoolSizePerThread(std::size_t pool_size)
	{
		return (pool_size / kWorkeThreadsNum) / 4;
	}

	constexpr std::size_t MaxPoolSizePerThread(std::size_t pool_size)
	{
		return 3 * (pool_size / kWorkeThreadsNum) / 8;
	}
}