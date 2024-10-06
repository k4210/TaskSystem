#pragma once

#include "LockFree.h"
#include <cstdlib>
#include <iostream>
namespace ts
{
	struct BlockHeader
	{
		constexpr static std::size_t allocation_offset = 2 * alignof(std::max_align_t);

		std::size_t size_ = 0; // including block header
		BlockHeader* next_ = nullptr;

		BlockHeader() = default;
		BlockHeader(BlockHeader&&) = delete;
		BlockHeader(const BlockHeader&) = delete;
		BlockHeader& operator=(BlockHeader&&) = delete;
		BlockHeader& operator=(const BlockHeader&) = delete;

		uint8* GetMemory() const
		{
			return const_cast<uint8*>(reinterpret_cast<const uint8*>(this)) + allocation_offset;
		}

		static BlockHeader* FromMemoryPointer(uint8* ptr)
		{
			return reinterpret_cast<BlockHeader*>(ptr - allocation_offset);
		}
	};

	static_assert(sizeof(BlockHeader) <= BlockHeader::allocation_offset);

	struct MemoryBlocksCollection
	{
		BlockHeader& allocate()
		{
			BlockHeader* block = free_.Pop();
			if (!block)
			{
				void* ptr = std::malloc(block_size_);
				assert(ptr);
				block = new (ptr) BlockHeader{};
				DEBUG_CODE(counter_++;)
			}
			return *block;
		}

		void deallocate(BlockHeader& block)
		{
			assert(!block.next_);
			block.size_ = 0;
			free_.Push(block);
		}

		MemoryBlocksCollection(std::size_t in_block_size)
			: block_size_(in_block_size)
		{}

		~MemoryBlocksCollection()
		{
			uint32 local_counter = 0;
			while (BlockHeader* block = free_.Pop())
			{
				std::destroy_at(block);
				std::free(block);
				local_counter++;
			}
			assert(local_counter == counter_);
			DEBUG_CODE(std::cout << "Simple allocator " << block_size_ << " max usage: " << counter_.load() << std::endl;)
		}

		void ensure_all_free()
		{
			uint32 local_counter = 0;
			lock_free::PointerBasedStack<BlockHeader> temp;
			while (BlockHeader* block = free_.Pop())
			{
				temp.Push(*block);
				local_counter++;
			}
			assert(local_counter == counter_);
			while (BlockHeader* block = temp.Pop())
			{
				free_.Push(*block);
			}
		}


		const std::size_t block_size_; //including header

	private:
		lock_free::PointerBasedStack<BlockHeader> free_;

		DEBUG_CODE(std::atomic<uint32> counter_ = 0;)

	};

	class SimpleAllocator
	{
	public:
		uint8* allocate(const std::size_t in_size)
		{
			const std::size_t size_with_header = in_size + BlockHeader::allocation_offset;
			BlockHeader& block = [&]() -> BlockHeader&
				{
					for (MemoryBlocksCollection& collection : block_collections)
					{
						if (size_with_header <= collection.block_size_)
						{
							return collection.allocate();
						}
					}

					uint8* external_allocation = reinterpret_cast<uint8*>(std::malloc(size_with_header));
					assert(external_allocation);
					BlockHeader* block = new (external_allocation) BlockHeader{};
					return *block;
				}();
			block.size_ = size_with_header;
			return block.GetMemory();
		}

		void deallocate(uint8* ptr)
		{
			assert(ptr);
			BlockHeader* block = BlockHeader::FromMemoryPointer(ptr);
			assert(block);
			const std::size_t size_with_header = block->size_;

			for (MemoryBlocksCollection& collection : block_collections)
			{
				if (size_with_header <= collection.block_size_)
				{
					return collection.deallocate(*block);
				}
			}

			std::destroy_at(block);
			std::free(ptr);
		}

		void ensure_all_free()
		{
			for (auto& collecion : block_collections)
			{
				collecion.ensure_all_free();
			}
		}

	private:
		std::array<MemoryBlocksCollection, 3> block_collections = { 1024ull, 4 * 1024ull, 16 * 1024ull };
	};

}