#include "Coroutine.h"
#include "SimpleAllocator.h"

namespace Coroutine::detail
{
	SimpleAllocator simple_allocator;

	void* do_allocate(std::size_t size)
	{
		return simple_allocator.allocate(size);
	}

	void do_deallocate(void* ptr)
	{
		if (!ptr)
		{
			assert(false);
			return;
		}
		simple_allocator.deallocate(reinterpret_cast<uint8*>(ptr));
	}

	void ensure_allocator_free()
	{
		simple_allocator.ensure_all_free();
	}
}