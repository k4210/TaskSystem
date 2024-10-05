#pragma once

#include "RefCount.h"
#include "Pool.h"

namespace utils
{
	//Node must be both ref counted and pooled
	template<typename Node>
	class TRefCountPoolPtr
	{
	public:

		TRefCountPoolPtr() = default;

		TRefCountPoolPtr(Node& node)
		{
			index_ = GetPoolIndex(node);
			node.AddRef();
		}

		TRefCountPoolPtr(Node* node)
		{
			if (node)
			{
				index_ = GetPoolIndex(*node);
				node->AddRef();
			}
		}

		TRefCountPoolPtr(Index index_, bool add_ref = true)
			: index_(index_)
		{
			if (IsValid() && add_ref)
			{
				FromPoolIndex<Node>(index_).AddRef();
			}
		}

		TRefCountPoolPtr(const TRefCountPoolPtr& copy)
			: TRefCountPoolPtr(copy.Get())
		{}

		TRefCountPoolPtr(TRefCountPoolPtr&& move)
		{
			index_ = move.index_;
			move.index_ = kInvalidIndex;
		}

		~TRefCountPoolPtr()
		{
			if (Node* node = Get())
			{
				node->Release();
			}
		}

		void ResetNoRelease()
		{
			index_ = kInvalidIndex;
		}

#pragma region TRefCountPtr
		TRefCountPoolPtr(const TRefCountPtr<Node>& other)
			: TRefCountPoolPtr(other.Get())
		{}

		TRefCountPoolPtr& operator=(const TRefCountPtr<Node>& other)
		{
			return *this = other.Get();
		}

		TRefCountPtr<Node> ToRefCountPtr()&
		{
			return TRefCountPtr(Get());
		}

		TRefCountPtr<Node> ToRefCountPtr()&&
		{
			Node* node = Get();
			ResetNoRelease();
			return TRefCountPtr(node, false);
		}

#pragma endregion
		TRefCountPoolPtr& operator=(std::nullptr_t)
		{
			Node* old_node = Get();
			index_ = kInvalidIndex;
			if (old_node)
			{
				old_node->Release();
			}
			return *this;
		}

		TRefCountPoolPtr& operator=(Node* new_node)
		{
			Node* old_node = Get();
			if (old_node != new_node)
			{
				// Call AddRef before Release, in case the new reference is the same as the old reference.
				index_ = new_node ? GetPoolIndex(*new_node) : kInvalidIndex;
				if (new_node)
				{
					new_node->AddRef();
				}
				if (old_node)
				{
					old_node->Release();
				}
			}
			return *this;
		}

		TRefCountPoolPtr& operator=(const TRefCountPoolPtr& InPtr)
		{
			return *this = InPtr.Get();
		}

		TRefCountPoolPtr& operator=(TRefCountPoolPtr&& move)
		{
			Node* old_node = Get();
			index_ = move.index_;
			move.index_ = kInvalidIndex;
			if (old_node)
			{
				old_node->Release();
			}
			return *this;
		}

		friend void swap(TRefCountPoolPtr& A, TRefCountPoolPtr& B)
		{
			Index AIdx = A.index_;
			A.index_ = B.index_;
			B.index_ = AIdx;
		}

		bool IsValid() const
		{
			return index_ != kInvalidIndex;
		}

		Node* Get() const
		{
			return IsValid()
				? &FromPoolIndex<Node>(index_)
				: nullptr;
		}

		Node* operator->() const
		{
			Node* node = Get();
			assert(node);
			return node;
		}

		operator Node* () const
		{
			return Get();
		}

		operator bool() const
		{
			return IsValid();
		}

		uint32 GetRefCount() const
		{
			Node* node = Get();
			return node ? node->GetRefCount() : 0;
		}

		bool operator==(const TRefCountPoolPtr& B) const = default;

		bool operator==(Node* B) const
		{
			return Get() == B;
		}

	private:
		Index index_ = kInvalidIndex;
	};

}