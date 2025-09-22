#include <cstdlib>
#include <SFML/Graphics.hpp>
#include <vector>
#include <iostream>

float SignedAreaTwice(const sf::Vector2f P, const sf::Vector2f Q, const sf::Vector2f R)
{
	return (Q.x - P.x) * (R.y - P.y) - (Q.y - P.y) * (R.x - P.x);
}

bool Intersect(const sf::Vector2f p1, const sf::Vector2f p2, 
    const sf::Vector2f q1, const sf::Vector2f q2, 
    float& outAlphaP, float& outAlphaQ)
{
	const auto AP1 = SignedAreaTwice(p1, q1, q2);
	const auto AP2 = SignedAreaTwice(p2, q1, q2);
    if (AP1 * AP2 <= 0)
    {
		const auto AQ1 = SignedAreaTwice(q1, p1, p2);
        const auto AQ2 = SignedAreaTwice(q2, p1, p2);
        if (AQ1 * AQ2 <= 0)
        {
            const auto denomP = AP1 - AP2;
            outAlphaP = (denomP != 0) ? (AP1 / denomP) : 0.0f;
            const auto denomQ = AQ1 - AQ2;
            outAlphaQ = (denomQ != 0) ? (AQ1 / denomQ) : 0.0f;
            return true;
		}
    }
    return false;
}

bool PointInsidePolygon(const sf::Vector2f R, const std::vector<sf::Vector2f>& poly)
{
	uint32_t w = 0;
    for(int idx = 0; idx < poly.size(); idx++)
    {
        const sf::Vector2f P0 = poly[idx];
        const sf::Vector2f P1 = poly[(idx + 1) % poly.size()];

        if ( (P0.y < R.y) != (P1.y < R.y) )
        {
			if (P0.x >= R.x) 
            {
				if (P1.x > R.x)
					w += 2 * (P1.y > P0.y) - 1;
				else if ( (SignedAreaTwice(P0,P1,R) > 0) == (P1.y > P0.y) )
						w += 2 * (P1.y > P0.y) - 1;
			}
			else if (P1.x > R.x)
            {
				if ( (SignedAreaTwice(P0,P1,R) > 0) == (P1.y > P0.y) )
					w += 2 * (P1.y > P0.y) - 1;
            }
        }
    }
    return ( (w % 2) != 0 );
}

sf::Vector2f Lerp(const sf::Vector2f& p1, const sf::Vector2f& p2, float alpha)
{
    return p1 + alpha * (p2 - p1);
}

// T must have T* next and T* prev members
template<typename T>
struct IntrinsicDoubleLinkedList
{
    void pushBack(T* node)
    {
        assert(node);
        if (m_tail)
        {
            node->prev = m_tail;
            m_tail->next = node;
            m_tail = node;
        }
        else
        {
            m_head = node;
            m_tail = node;
        }
	}

    void pushAfter(T* after, T* node)
    {
        assert(after);
        assert(node);
        node->prev = after;
        node->next = after->next;
        after->next = node;
        if (node->next)
            node->next->prev = node;
        else
        {
            assert(m_tail == after);
            m_tail = node;
        }
	}

    T* head() const { return m_head; }
	T* tail() const { return m_tail; }

    ~IntrinsicDoubleLinkedList()
    {
        T* current = m_head;
        while (current)
        {
            T* next = current->next;
            delete current;
            current = next;
        }
        m_head = nullptr;
        m_tail = nullptr;
    }

private:
	T* m_head = nullptr;
	T* m_tail = nullptr;
};

std::vector<sf::Vector2f> SumPoly(const std::vector<sf::Vector2f>& polyA, const std::vector<sf::Vector2f>& polyB)
{
    struct VertexNode
    {
        sf::Vector2f position;
        VertexNode* next = nullptr;
		VertexNode* prev = nullptr;
		VertexNode* nextPoly = nullptr; // next vertex in the other polygon
        VertexNode* neighbor = nullptr;
		float alpha = 0.0f; // position on the edge
        bool intersect = false;
		bool entry = false; // true: entry, false: exit
    };

	IntrinsicDoubleLinkedList<VertexNode> listA, listB;

    for(auto p : polyA)
		listA.pushBack(new VertexNode{ p });

	for (auto p : polyB)
		listB.pushBack(new VertexNode{ p });

	//STAGE 1: find intersection points
	for (VertexNode* nodeA = listA.head(); nodeA != nullptr; nodeA = nodeA->next)
    {
        auto skipIntersections = [](VertexNode*& node) -> bool
        {
            while (node && node->intersect)
                node = node->next;
			return node != nullptr;
		};
        if (!skipIntersections(nodeA)) 
            break;

		VertexNode* nodeA1 = nodeA->next;
        if (!skipIntersections(nodeA1)) 
			nodeA1 = listA.head();
		assert(nodeA1 && !nodeA1->intersect);

        for (VertexNode* nodeB = listB.head(); nodeB != nullptr; nodeB = nodeB->next)
        {
            if (!skipIntersections(nodeB)) 
				break;

            VertexNode* nodeB1 = nodeB->next;
            if (!skipIntersections(nodeB1)) 
				nodeB1 = listB.head();
            assert(nodeB1 && !nodeB1->intersect);

            float alphaA, alphaB;
            if (Intersect(nodeA->position, nodeA1->position, nodeB->position, nodeB1->position, alphaA, alphaB))
            {
				VertexNode* newA = new VertexNode{ Lerp(nodeA->position, nodeA1->position, alphaA) };
				newA->alpha = alphaA;
				newA->intersect = true;
				VertexNode* newB = new VertexNode{ Lerp(nodeB->position, nodeB1->position, alphaB) };
				newB->alpha = alphaB;
				newB->intersect = true;

				newA->neighbor = newB;
				newB->neighbor = newA;

                auto pushSorted = [](IntrinsicDoubleLinkedList<VertexNode>& list, VertexNode* start, VertexNode* node)
                {
                    VertexNode* curr = start;
                    while (curr->next && curr->next->intersect && (curr->next->alpha < node->alpha))
                        curr = curr->next;
                    list.pushAfter(curr, node);
				};

				pushSorted(listA, nodeA, newA);
				pushSorted(listB, nodeB, newB);
            }
        }
    }

	//STAGE 2: entry / exit
    auto fillEntryExit = [&](IntrinsicDoubleLinkedList<VertexNode>& list, const std::vector<sf::Vector2f>& otherPoly)
    {
        bool inside = PointInsidePolygon(list.head()->position, otherPoly);
        assert(!list.head()->intersect);
        for (VertexNode* node = list.head(); node; node = node->next)
        {
            if (node->intersect)
            {
                node->entry = !inside;
                inside = !inside;
            }
		}
    };
	fillEntryExit(listA, polyB);
	fillEntryExit(listB, polyA);

	//STAGE 3: traverse to construct result
    auto constructResult = [&listA, &listB]() -> std::vector<sf::Vector2f>
    {
        std::vector<sf::Vector2f> result;
	    VertexNode* current = listA.head();
	    while (current && !current->intersect) //find first intersection
		    current = current->next;
	    if (!current)
		    return result;

	    VertexNode* closedLoop = current;
	    bool directionForward = true;
	    bool usingA = true;
	    uint32_t loopCount = 0;
        do
        {
            result.push_back(current->position);

            if(current->intersect)
            {
                //ADD
                current = current->neighbor;
			    directionForward = !current->entry;
			    usingA = !usingA;

                // B - A
                /*
                if (usingA)
                {
                    if (current->entry != directionForward)
                    {
                        current = current->neighbor;
                        directionForward = !current->entry;
                        usingA = false;
                    }
                } 
                else
                {
                    if (current->entry == directionForward)
                    {
                        current = current->neighbor;
                        directionForward = current->entry;
                        usingA = true;
                    }
			    }
                */
            }

            current = directionForward 
			    ? (current->next ? current->next : (usingA ? listA.head() : listB.head()))
			    : (current->prev ? current->prev : (usingA ? listA.tail() : listB.tail()));
        }
        while((current != closedLoop) && (current != closedLoop->neighbor) && (loopCount++ < 10000));
		if(loopCount >= 10000)
		    std::cerr << "Tnfinite loop detected in polygon sum!" << std::endl;

        return result;
    };
   
    return constructResult();
}

int main()
{
    constexpr uint32_t resolution = 1024;
    sf::RenderWindow window(sf::VideoMode(sf::Vector2u(resolution, resolution)), "Anthill");
    window.display();
    window.clear();

    std::vector<sf::Vector2f> poly1 = {
        sf::Vector2f(20.0f, 20.0f), 
        sf::Vector2f(20.0f, 200.0f), 
        sf::Vector2f(200.0f, 200.0f), 
        sf::Vector2f(200.0f, 20.0f) };
    std::vector<sf::Vector2f> poly2 = {
        sf::Vector2f(40.0f, 40.0f), 
        sf::Vector2f(40.0f, 300.0f), 
        sf::Vector2f(300.0f, 300.0f), 
        sf::Vector2f(300.0f, 40.0f) };
    std::vector<sf::Vector2f> polySum;

    int selectedPoint = -1;

    while (window.isOpen())
    {
        while (auto event = window.pollEvent())
        {
            if (event->is<sf::Event::Closed>())
            {
                window.close();
                return 0;
            }

            if (auto pressed = event->getIf<sf::Event::MouseButtonPressed>(); pressed && (pressed->button == sf::Mouse::Button::Left))
            {
                sf::Vector2f pos = window.mapPixelToCoords(pressed->position);
                for(int idx = 0; idx < poly1.size(); idx++)
                {
                    if ((poly1[idx] - pos).length() < 10.0f)
                    {
                        selectedPoint = idx;
		                break;
                    }
                }
            }

            else if (auto release = event->getIf<sf::Event::MouseButtonReleased>(); release && (release->button == sf::Mouse::Button::Left))
            {
                selectedPoint = -1;
            }

            else if (auto move = event->getIf<sf::Event::MouseMoved>(); move && (selectedPoint >= 0))
            {
				poly1[selectedPoint] = window.mapPixelToCoords(move->position);
				polySum = SumPoly(poly1, poly2);
            }
        }

        window.display();
        window.clear();

        auto drawPoly = [&](const std::vector<sf::Vector2f>& poly, sf::Color color, sf::Vector2f offset = sf::Vector2f(0.0f, 0.0f))
        {
            for(int idx = 0; idx < poly.size(); idx++)
            {
                const sf::Vector2f p1 = poly[idx] + offset;

                sf::CircleShape shape;
                shape.setPosition(p1 -  sf::Vector2f(10.0f, 10.0f));
                shape.setRadius(10.0f);
				shape.setFillColor(color);
                window.draw(shape);

                const sf::Vector2f p2 = poly[(idx + 1) % poly.size()] + offset;
                std::array line = { sf::Vertex{p1, color}, sf::Vertex{p2, color} };
				window.draw(line.data(), line.size(), sf::PrimitiveType::Lines);
            }
        };

		drawPoly(poly1, sf::Color::White);
		drawPoly(poly2, sf::Color::Green);

		drawPoly(polySum, sf::Color::Red);//, sf::Vector2f(400.0f, 400.0f));
    }
	return 0;
}