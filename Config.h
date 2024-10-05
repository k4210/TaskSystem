#pragma once

#include <cstddef>

#define COROUTINE_CUSTOM_ALLOC 1

#define THREAD_SMART_POOL 1

#ifdef NDEBUG
#define DO_POOL_STATS 0
#else
#define DO_POOL_STATS 1
#endif // _DEBUG

constexpr std::size_t k_workers_num = 8;
constexpr std::size_t k_task_pool = 2048;
constexpr std::size_t k_dep_node_pool = 2048;
constexpr std::size_t k_future_pool = 2048;