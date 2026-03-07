#pragma once

#include <atomic>

namespace ts
{
    class SpinMutex
    {
        std::atomic_flag m_{};
 
      public:
        void lock() noexcept
        {
            while (m_.test_and_set(std::memory_order_acquire))
            {
                // Since C++20, locks can be acquired only after notification in the unlock,
                // avoiding any unnecessary spinning.
                // Note that even though wait guarantees it returns only after the value has
                // changed, the lock is acquired after the next condition check.
                m_.wait(true, std::memory_order_relaxed);
            }
        }
        bool try_lock() noexcept
        {
            return !m_.test_and_set(std::memory_order_acquire);
        }
        void unlock() noexcept
        {
            m_.clear(std::memory_order_release);
            m_.notify_one();
        }
    };

}