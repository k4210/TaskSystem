#include "AccessSynchronizer.h"
#include "Config.h"
#include "Pool.h"
#include "Task.h"

DEBUG_CODE(thread_local bool ts::AccessSynchronizer::is_any_asset_locked_ = false;)

namespace ts
{
	Pool<AccessSynchronizer::CollectionNode, kSynchronizerNodePoolSize
#if THREAD_SMART_POOL
			, kWorkeThreadsNum, InitPoolSizePerThread(kSynchronizerNodePoolSize), MaxPoolSizePerThread(kSynchronizerNodePoolSize)
#endif
		>g_synchronizer_nodes_pool;

	std::span<AccessSynchronizer::CollectionNode> AccessSynchronizer::CollectionNode::GetPoolSpan()
	{
		return g_synchronizer_nodes_pool.GetPoolSpan();
	}

	Index AccessSynchronizer::CollectionNode::Acquire()
	{
		auto& node = g_synchronizer_nodes_pool.Acquire();
		return GetPoolIndex(node);
	}

	void AccessSynchronizer::CollectionNode::Release(Index index)
	{
		g_synchronizer_nodes_pool.Return(FromPoolIndex<CollectionNode>(index));
	}

	void AccessSynchronizer::CollectionNode::ReleaseChain(Index head)
	{
		assert(head != kInvalidIndex);
		Index current = head;
		uint16 len = 0;
		while (true)
		{
			CollectionNode& node = FromPoolIndex<CollectionNode>(current);
			len++;
			if (node.next_ == kInvalidIndex)
			{
				break;
			}
			current = node.next_;
		}
		g_synchronizer_nodes_pool.ReturnChain(
			FromPoolIndex<CollectionNode>(head),
			FromPoolIndex<CollectionNode>(current),
			len);
	}

	void SyncMultiResult::HandleOnTask(const SyncMultiResult result, BaseTask& task)
	{
		if (!result.len)
		{
			TaskSystem::HandlePrerequires(task);
			return;
		}

		const size_t mem_size_gates = result.len * sizeof(Gate*);
		const size_t mem_size_tags = result.len * sizeof(uint8);

		uint8* raw_mem = (uint8*)alloca(mem_size_gates + mem_size_tags);
		Gate** pre_req = reinterpret_cast<Gate**>(raw_mem);
		uint8* tags = reinterpret_cast<uint8*>(raw_mem + mem_size_gates);

		Index current = result.head_;
		for(uint32 idx = 0; idx < result.len; idx++)
		{
			AccessSynchronizer::CollectionNode& node = FromPoolIndex<AccessSynchronizer::CollectionNode>(current);
			pre_req[idx] = node.task_->GetGate();
			assert(pre_req[idx] && pre_req[idx]->GetState() != ETaskState::Nonexistent_Pooled);
			tags[idx] = node.task_tag_;
			assert((node.next_ == kInvalidIndex) == (idx == result.len - 1));
			current = node.next_;
		}

		TaskSystem::HandlePrerequires(task, std::span<Gate*>(pre_req, result.len), std::span<uint8>(tags, result.len));

		AccessSynchronizer::CollectionNode::ReleaseChain(result.head_);
	}
}