#include "Coroutine.h"
#include "Task.h"
#include "AccessSynchronizer.h"
#include "TickSync.h"
#include "Profiling.h"
#include <cstdlib>

#include <SFML/Graphics.hpp>

using namespace ts;

TickSync tick_sync;
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
constexpr float resolution = 1600;

TDetachCoroutine render(GraphicContainer& graphic_container)
{
    sf::Font font;
    font.loadFromFile("../../tuffy.ttf");

    sf::RenderWindow window(sf::VideoMode(1600, 1600), "Anthill");
    window.display();

    TickScope tick_scope(tick_sync);
    double ms_duration = 0;
    double ms_duration_graphic = 0;
    while (window.isOpen())
    {
        TimeType start_time = GetTime();
        sf::Event event;
        while (window.pollEvent(event))
        {
            if (event.type == sf::Event::Closed)
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
            sf::Text text;
            text.setFont(font);
            text.setString(std::to_string(ms_duration)+"/"+ std::to_string(ms_duration_graphic));
            text.setCharacterSize(48);
            text.setFillColor(sf::Color::Red);
            window.draw(text);
        }();
        window.display();
        window.setActive(false);
        ms_duration_graphic = ToMiliseconds(GetTime() - start_time);
        co_await tick_scope.WaitForNextFrame();
        ms_duration = ToMiliseconds(GetTime() - start_time);
    }
}

TDetachCoroutine ant(GraphicContainer& graphic_container, uint32 idx)
{
    float x = std::abs(std::rand()) / resolution;
    float y = std::abs(std::rand()) / resolution;

    float dx = (std::rand() % 1024) / 1024.0f;
    float dy = (std::rand() % 1024) / 1024.0f;

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
            //AccessScopeCo<GraphicContainer*> guard = co_await graphic_container;
            sf::CircleShape& shape = graphic_container.GetWrite()[idx];
            shape.setPosition(x, y);
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

    tick_sync.Initialize([&](uint32)
        {
            graphic_container.OnFrameReady();
        });

    TaskSystem::AsyncResume(render(graphic_container));

    //SyncHolder<GraphicContainer*> graphic_holder(&graphic_container);
    for (uint32 idx = 0; idx < number_ants; idx++)
    {
        TaskSystem::AsyncResume(ant(graphic_container, idx));
    }

    TaskSystem::WaitForWorkerThreadsToJoin();
    detail::ensure_allocator_free();

    return 0;
}