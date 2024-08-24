#pragma once

#include <atomic>
#include <assert.h>
#include "BaseTypes.h"

template< typename DerivedType >
struct TRefCounted
{
	TRefCounted() = default;

	~TRefCounted()
	{
		assert(RefCount.load() == 0);
	}

	TRefCounted(const TRefCounted&) = delete;
	TRefCounted(TRefCounted&&) = delete;
	TRefCounted& operator = (const TRefCounted&) = delete;
	TRefCounted& operator = (TRefCounted&&) = delete;

	uint32 AddRef() const
	{
		const int32 NewRefCount = ++RefCount;
		assert(NewRefCount >= 1);

		return static_cast<uint32>(NewRefCount);
	}

	uint32 Release() const
	{
		const int32 NewRefCount = --RefCount;
		assert(NewRefCount >= 0);
		const uint32 Refs = static_cast<uint32>(NewRefCount);
		if (Refs == 0)
		{
			DerivedType* const Derived = const_cast<DerivedType*>(static_cast<const DerivedType*>(this));

			if constexpr (requires { Derived->OnRefCountZero(); })
			{
				Derived->OnRefCountZero();
			}
			else
			{
				delete Derived;
			}
		}

		return Refs;
	}

	uint32 GetRefCount() const
	{
		return static_cast<uint32>(RefCount.load());
	}

private:
	mutable std::atomic< int32 > RefCount = 0;
};

template<typename ReferencedType>
class TRefCountPtr
{
public:

	TRefCountPtr() = default;

	TRefCountPtr(ReferencedType* InReference)
	{
		Reference = InReference;
		if (Reference)
		{
			Reference->AddRef();
		}
	}

	TRefCountPtr(ReferencedType& InReference)
	{
		Reference = &InReference;
		Reference->AddRef();
	}

	TRefCountPtr(const TRefCountPtr& Copy)
		: TRefCountPtr(Copy.Get())
	{}

	TRefCountPtr(TRefCountPtr&& Move)
	{
		Reference = Move.Get();
		Move.Reference = nullptr;
	}

	template<typename OtherType>
	explicit TRefCountPtr(TRefCountPtr<OtherType>&& Move)
	{
		Reference = static_cast<ReferencedType*>(Move.Get());
		Move.Reference = nullptr;
	}

	~TRefCountPtr()
	{
		if (Reference)
		{
			Reference->Release();
		}
	}

	TRefCountPtr& operator=(ReferencedType* InReference)
	{
		if (Reference != InReference)
		{
			// Call AddRef before Release, in case the new reference is the same as the old reference.
			ReferencedType* OldReference = Reference;
			Reference = InReference;
			if (Reference)
			{
				Reference->AddRef();
			}
			if (OldReference)
			{
				OldReference->Release();
			}
		}
		return *this;
	}

	TRefCountPtr& operator=(const TRefCountPtr InPtr)
	{
		return *this = InPtr.Get();
	}

	TRefCountPtr& operator=(TRefCountPtr&& InPtr)
	{
		ReferencedType* OldReference = Reference;
		Reference = InPtr.Get();
		InPtr.Reference = nullptr;
		if (OldReference)
		{
			OldReference->Release();
		}
		return *this;
	}

	friend void swap(TRefCountPtr& A, TRefCountPtr& B)
	{
		ReferencedType* OldAReference = A.Reference;
		A.Reference = B.Reference;
		B.Reference = OldAReference;
	}

	ReferencedType* operator->() const
	{
		assert(Reference);
		return Reference;
	}

	operator ReferencedType* () const
	{
		return Reference;
	}

	ReferencedType* Get() const
	{
		return Reference;
	}

	bool IsValid() const
	{
		return Reference != nullptr;
	}

	operator bool() const
	{
		return IsValid();
	}

	uint32 GetRefCount() const
	{
		return Reference
			? Reference->GetRefCount()
			: 0;
	}

	bool operator==(const TRefCountPtr& B) const = default;

	bool operator==(ReferencedType* B) const
	{
		return Get() == B;
	}

private:
	ReferencedType* Reference = nullptr;

	template <typename OtherType>
	friend class TRefCountPtr;
};

namespace std
{
	template<typename T>
	struct hash<TRefCountPtr<T>>
	{
		size_t operator()(const TRefCountPtr<T>& Ptr) const { return hash(Ptr.Get()); }
	};
}
