#include "Config.h"

#if ANT_HILL || ANT_HILL_STD

#include "Profiling.h"
#include <cstdlib>
#include <SFML/Graphics.hpp>

constexpr uint32 number_ants = 3 * 1024;
constexpr uint32 resolution = 1024;

#endif // ANT_HILL || ANT_HILL_STD

#if ANT_HILL

#include "Coroutine.h"
#include "Task.h"
#include "AccessSynchronizer.h"
#include "TickSync.h"

using namespace ts;
bool g_running = true;

struct GraphicContainer
{
    AccessSynchronizer synchronizer_;
    std::vector<sf::CircleShape> components_;

    std::vector<sf::CircleShape>& GetWrite()
    {
        return components_;
    }

    const std::vector<sf::CircleShape>& GetRead() const
    {
        return components_;
    }
};

TDetachCoroutine render(SyncHolder<GraphicContainer> in_graphic_container, TickSync& tick_sync)
{
    sf::Font font("tuffy.ttf");

    sf::RenderWindow window(sf::VideoMode(sf::Vector2u(resolution, resolution)), "Anthill");
    window.display();
    window.clear();
    AvgTime event_avg, render_avg, wait_avg, exclusive_avg;
    TickScope tick_scope(tick_sync);
    TimeType time_begin = GetTime();
    while (window.isOpen())
    {
        while (auto event = window.pollEvent())
        {
            if (event->is<sf::Event::Closed>())
            {
                TaskSystem::StopWorkerThreadsNoWait();
                window.close();
                g_running = false;
                co_return;
            }
        }

        const TimeType time_rendering = event_avg.add(time_begin);

        {
            bool success = window.setActive(false);
            assert(success);

            AccessScopeCo<GraphicContainer> graphic_container = co_await in_graphic_container;
            TimeType exlusive_scope_begin = GetTime();
            //success = window.setActive(true);
            //assert(success);
            for (const sf::CircleShape& shape : graphic_container->GetRead())
            {
                window.draw(shape);
            }
            exclusive_avg.add(exlusive_scope_begin);
        }
        [&]() // Draw FPS
        {
            sf::Text text(
                font,
                "event: " + std::to_string(event_avg.average())
                + " render: " + std::to_string(render_avg.average()) 
                + " wait: " + std::to_string(wait_avg.average())
                + " exclusive: " + std::to_string(exclusive_avg.average()),
                24);
            text.setFillColor(sf::Color::Red);
            window.draw(text);
        }();
        window.display();
        window.clear();
        bool success = window.setActive(false);
        assert(success);
		const TimeType time_waiting = render_avg.add(time_rendering);
        co_await tick_scope.WaitForNextFrame();
        //success = window.setActive(true);
		//assert(success);
        time_begin = wait_avg.add(time_waiting);
    }
}

TDetachCoroutine ant(SyncHolder<GraphicContainer> in_graphic_container, uint32 idx, TickSync& tick_sync)
{
    std::srand(idx);
    float x = static_cast<float>(std::abs(std::rand()) % resolution);
    float y = static_cast<float>(std::abs(std::rand()) % resolution);

    constexpr float speed = 1.0f;
    float dx = speed * ((std::rand() % 1024) - 512) / 512.0f;
    float dy = speed * ((std::rand() % 1024) - 512) / 512.0f;

    TickScope tick_scope(tick_sync);
    while (g_running)
    {
        auto update = [](float& v, float& dv)
        {
            v += dv;
            if (v < 0)
            {
                v = 0;
                dv = -dv;
            }
            else if (v > resolution)
            {
                v = resolution;
                dv = -dv;
            }
        };
        update(x, dx);
        update(y, dy);

        {
            SharedAccessScopeCo<GraphicContainer> graphic_container = co_await in_graphic_container.Shared();
            sf::CircleShape& shape = graphic_container->GetWrite()[idx];
            shape.setPosition(sf::Vector2f(x, y));
            shape.setRadius(10.0f);
        }
        co_await tick_scope.WaitForNextFrame();
    }

}

int main()
{
    TaskSystem::StartWorkerThreads();

    GraphicContainer graphic_container;
    graphic_container.components_.resize(number_ants);

    TickSync tick_sync;
    tick_sync.Initialize([&](uint32)
        {
           // graphic_container.OnFrameReady();
        });

    SyncHolder<GraphicContainer> guarded_graphic_container{&graphic_container};
    TaskSystem::AsyncResume(render(guarded_graphic_container, tick_sync));

    //SyncHolder<GraphicContainer*> graphic_holder(&graphic_container);
    for (uint32 idx = 0; idx < number_ants; idx++)
    {
        TaskSystem::AsyncResume(ant(guarded_graphic_container, idx, tick_sync));
    }

    TaskSystem::WaitForWorkerThreadsToJoin();
    detail::ensure_allocator_free();

    return 0;
}

#endif // ANT_HILL

#if ANT_HILL_STD

#include <thread>
#include <barrier>


struct GraphicContainer
{
    //AccessSynchronizer synchronizer_;
    std::vector<sf::CircleShape> components_[2];
    uint8 write_idx_ = 0;

    void OnFrameReady()
    {
        write_idx_ = 1 - write_idx_;
    }

    std::vector<sf::CircleShape>& GetWrite()
    {
        return components_[write_idx_];
    }

    const std::vector<sf::CircleShape>& GetRead() const
    {
        return components_[1 - write_idx_];
    }
};


int main()
{
    using namespace ts;

    GraphicContainer graphic_container;
    graphic_container.components_[0].resize(number_ants);
    graphic_container.components_[1].resize(number_ants);

    sf::Font font("tuffy.ttf");

    sf::RenderWindow window(sf::VideoMode(sf::Vector2u(resolution, resolution)), "Anthill");
    window.display();

    auto on_frame_ready = [&graphic_container]() noexcept
    {
        graphic_container.OnFrameReady();
    };
    std::barrier barrier(number_ants+1, on_frame_ready);

    bool g_running = true;
    auto ant = [&](uint32 idx) noexcept
    {
        std::srand(idx);
        float x = static_cast<float>(std::abs(std::rand()) % resolution);
        float y = static_cast<float>(std::abs(std::rand()) % resolution);

        constexpr float speed = 1.0f;
        float dx = speed * ((std::rand() % 1024) - 512) / 512.0f;
        float dy = speed * ((std::rand() % 1024) - 512) / 512.0f;

        while (g_running)
        {
            auto update = [](float& v, float& dv)
            {
                v += dv;
                if (v < 0)
                {
                    v = 0;
                    dv = -dv;
                }
                else if (v > resolution)
                {
                    v = resolution;
                    dv = -dv;
                }
            };
            update(x, dx);
            update(y, dy);

            {
                sf::CircleShape& shape = graphic_container.GetWrite()[idx];
                shape.setPosition(sf::Vector2f(x, y));
                shape.setRadius(10.0f);
            }
            barrier.arrive_and_wait();
        }
    };

	std::array<std::jthread, number_ants> ants_workers;
    for (uint32 idx = 0; idx < number_ants; idx++)
    {
        ants_workers[idx] = std::jthread(ant, idx);
	}

    AvgTime event_avg, render_avg, wait_avg;
    TimeType time_begin = GetTime();

    while (window.isOpen())
    {
        while (auto event = window.pollEvent())
        {
            if (event->is<sf::Event::Closed>())
            { 
                g_running = false;
                break;
            }
        }
        if(!g_running)
        {
            barrier.arrive();
            break;
		}

        const TimeType time_rendering = event_avg.add(time_begin);

        window.clear();
        for (const sf::CircleShape& shape : graphic_container.GetRead())
        {
            window.draw(shape);
        }
        [&]() // Draw FPS
        {
            sf::Text text(
                font,
                "event: " + std::to_string(event_avg.average())
                + " render: " + std::to_string(render_avg.average()) 
                + " wait: " + std::to_string(wait_avg.average()),
                48);
            text.setFillColor(sf::Color::Red);
            window.draw(text);
        }();
        window.display();
        const bool success = window.setActive(false);
        assert(success);
        const TimeType time_waiting = render_avg.add(time_rendering);
        barrier.arrive_and_wait();
        time_begin = wait_avg.add(time_waiting);
    }

    for(auto& worker : ants_workers)
    {
        worker.join();
	}

    window.close();

    return 0;
}

#endif // ANT_HILL_STD
