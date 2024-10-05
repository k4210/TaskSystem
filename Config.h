#pragma once

#include <cstddef>

#define COROUTINE_CUSTOM_ALLOC 1

#define THREAD_SMART_POOL 1

#ifdef NDEBUG
#define DO_POOL_STATS 0
#else
#define DO_POOL_STATS 1
#endif // _DEBUG

constexpr std::size_t kWorkeThreadsNum = 16;
constexpr std::size_t kTaskPoolSize = 2048;
constexpr std::size_t kDepNodePoolSize = 2048;
constexpr std::size_t kFuturePoolSize = 2048;