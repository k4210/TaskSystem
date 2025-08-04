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

	AccessSynchronizer::CollectionIndex AccessSynchronizer::CollectionNode::Acquire()
	{
		auto& node = g_synchronizer_nodes_pool.Acquire();
		return AccessSynchronizer::CollectionIndex{GetPoolIndex(node)};
	}

	void AccessSynchronizer::CollectionNode::Release(AccessSynchronizer::CollectionIndex index)
	{
		g_synchronizer_nodes_pool.Return(FromPoolIndex(index));
	}

	void AccessSynchronizer::CollectionNode::ReleaseChain(AccessSynchronizer::CollectionIndex head)
	{
		assert(head.IsValid());
		CollectionIndex current = head;
		uint16 len = 0;
		while (true)
		{
			CollectionNode& node = FromPoolIndex(current);
			len++;
			if (!node.NextRef().IsValid())
			{
				break;
			}
			current = node.NextRef();
		}
		g_synchronizer_nodes_pool.ReturnChain(
			FromPoolIndex(head),
			FromPoolIndex(current),
			len);
	}

	void AccessSynchronizer::SyncMultiResult::HandleOnTask(const AccessSynchronizer::SyncMultiResult result, BaseTask& task)
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

		CollectionIndex current = result.head_;
		for(uint32 idx = 0; idx < result.len; idx++)
		{
			AccessSynchronizer::CollectionNode& node = FromPoolIndex(current);
			pre_req[idx] = node.task_->GetGate();
			assert(pre_req[idx] && pre_req[idx]->GetState() != ETaskState::Nonexistent_Pooled);
			tags[idx] = node.task_tag_.RawValue();
			assert(node.NextRef().IsValid() != (idx == result.len - 1));
			current = node.NextRef();
		}

		TaskSystem::HandlePrerequires(task, std::span<Gate*>(pre_req, result.len), std::span<uint8>(tags, result.len));

		AccessSynchronizer::CollectionNode::ReleaseChain(result.head_);
	}
}