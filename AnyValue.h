#pragma once

#include <array>
#include <assert.h>
#include "BaseTypes.h"

template<uint32 Size>
struct AnyValue
{
	using ResultDeleterPtr = void (*)(AnyValue&);

private:

	static_assert(Size >= sizeof(void*));
	std::array<uint8, Size> data_;
	ResultDeleterPtr deleter_ = nullptr;

public:

	template<typename T> bool IsStoredInline()
	{
		return sizeof(T) > Size;
	}

	template<typename T> T*& ExternalStoragePointer()
	{
		return *reinterpret_cast<T**>(&data_);
	}

	template<typename T> T* InternalStoragePointer()
	{
		return reinterpret_cast<T*>(&data_);
	}

	template<typename T>
	struct DeleterHelper
	{
		static void Delete(AnyValue& val)
		{
			if (val.IsStoredInline<T>())
			{
				val.InternalStoragePointer<T>()->~T();
			}
			else
			{
				T* ptr = val.ExternalStoragePointer<T>();
				delete ptr;
			}
			val.ExternalStoragePointer<T>() = nullptr;
		}
	};

public:
	template<typename T> void Store(T&& value)
	{
		assert(!deleter_);
		if (IsStoredInline<T>())
		{
			new(InternalStoragePointer<T>()) T(std::forward<T>(value));
		}
		else
		{
			ExternalStoragePointer<T>() = new T(std::forward<T>(value));
		}

		deleter_ = DeleterHelper<T>::Delete;
	}

	template<typename T> T GetOnce()
	{
		assert(deleter_);
		T& ref = IsStoredInline<T>()
			? *InternalStoragePointer<T>()
			: *ExternalStoragePointer<T>();
		return std::move(ref);
	}

	bool HasValue() const
	{
		return !!deleter_;
	}

	void ForceReset()
	{
		assert(deleter_);
		deleter_(*this);
		deleter_ = nullptr;
	}

	void Reset()
	{
		if (deleter_)
		{
			ForceReset();
		}
	}

	~AnyValue()
	{
		Reset();
	}
};