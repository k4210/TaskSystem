#include "Coroutine.h"
#include "Task.h"
#include "AccessSynchronizer.h"
#include "TickSync.h"
#include "Profiling.h"
#include <cstdlib>
#if !TEST_MAIN
#include <SFML/Graphics.hpp>

using namespace ts;
bool g_running = true;

struct GraphicContainer
{
    AccessSynchronizer synchronizer_;
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

constexpr uint32 number_ants = 3 * 1024;
constexpr uint32 resolution = 1024;

TDetachCoroutine render(GraphicContainer& graphic_container, TickSync& tick_sync)
{
    sf::Font font("tuffy.ttf");

    sf::RenderWindow window(sf::VideoMode(sf::Vector2u(resolution, resolution)), "Anthill");
    window.display();

    TickScope tick_scope(tick_sync);
    double ms_duration = 0;
    double ms_duration_graphic = 0;
    while (window.isOpen())
    {
        TimeType start_time = GetTime();
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

        window.clear();
        for (const sf::CircleShape& shape : graphic_container.GetRead())
        {
            window.draw(shape);
        }
        [&]() // Draw FPS
        {
            sf::Text text(
                font,
                std::to_string(ms_duration)+"/"+ std::to_string(ms_duration_graphic),
                48);
            text.setFillColor(sf::Color::Red);
            window.draw(text);
        }();
        window.display();
        const bool success = window.setActive(false);
        assert(success);
        ms_duration_graphic = ToMiliseconds(GetTime() - start_time);
        co_await tick_scope.WaitForNextFrame();
        ms_duration = ToMiliseconds(GetTime() - start_time);
    }
}

TDetachCoroutine ant(GraphicContainer& graphic_container, uint32 idx, TickSync& tick_sync)
{
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
            sf::CircleShape& shape = graphic_container.GetWrite()[idx];
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
    graphic_container.components_[0].resize(number_ants);
    graphic_container.components_[1].resize(number_ants);

    TickSync tick_sync;
    tick_sync.Initialize([&](uint32)
        {
            graphic_container.OnFrameReady();
        });

    TaskSystem::AsyncResume(render(graphic_container, tick_sync));

    //SyncHolder<GraphicContainer*> graphic_holder(&graphic_container);
    for (uint32 idx = 0; idx < number_ants; idx++)
    {
        TaskSystem::AsyncResume(ant(graphic_container, idx, tick_sync));
    }

    TaskSystem::WaitForWorkerThreadsToJoin();
    detail::ensure_allocator_free();

    return 0;
}

#endif // !TEST_MAIN