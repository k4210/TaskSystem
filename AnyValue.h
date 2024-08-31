#pragma once

#include <type_traits>
#include <assert.h>
#include "BaseTypes.h"

// Eventually AnyValue could allocate external memory for big types. 
template<uint32 Size>
struct AnyValue
{
	using ResultDeleterPtr = void (*)(AnyValue&);

	template<typename T> static constexpr bool IsStoredInline()
	{
		return (sizeof(T) <= Size) && (alignof(T) <= alignof(void*));
	}

private:

	static_assert(Size >= sizeof(void*));
	std::aligned_storage_t<Size, alignof(void*)> data_;
	ResultDeleterPtr deleter_ = nullptr;

	template<typename T> T* InternalStoragePointer()
	{
		static_assert(IsStoredInline<T>());
		return reinterpret_cast<T*>(&data_);
	}

	template<typename T> const T* InternalStoragePointer() const
	{
		static_assert(IsStoredInline<T>());
		return reinterpret_cast<const T*>(&data_);
	}

	template<typename T>
	struct DeleterHelper
	{
		static void Delete(AnyValue& val)
		{
			val.InternalStoragePointer<T>()->~T();
		}
	};

public:
	template<typename T> void Store(T&& value)
	{
		assert(!deleter_);
		new(InternalStoragePointer<T>()) T(std::forward<T>(value));
		deleter_ = &DeleterHelper<T>::Delete;
	}

	// the stored value is left in undefined / "moved" state
	template<typename T> T GetOnce()
	{
		assert(deleter_);
		return std::move(*InternalStoragePointer<T>());
	}

	template<typename T> const T& Get() const
	{
		assert(deleter_);
		return *InternalStoragePointer<T>();
	}

	bool HasValue() const
	{
		return !!deleter_;
	}

	void Reset()
	{
		if (deleter_)
		{
			deleter_(*this);
			deleter_ = nullptr;
		}
	}

	~AnyValue()
	{
		Reset();
	}
};