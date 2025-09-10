#include <string_view>

template<size_t InSize = 2 * sizeof(void*)>
struct InplaceString
{
	static constexpr size_t kSize = std::max(1 + ((InSize - 1) / sizeof(void*)), 2 * sizeof(void*));

	InplaceString() = default;

	InplaceString(std::string_view str)
	{
		fromView(str);
	}

	InplaceString& operator= (std::string_view str)
	{
		if (!inplace() && 
			(calcSize() >= str.size()) && 
			(str.size() >= kSize))
		{
			std::memcpy(data_.ptr_, str.data(), str.size());
			data_.ptr_[str.size()] = 0;
			return;
		}

		freeAlloc(); 
		fromView(str);
	}

	InplaceString(InplaceString&& other)
	{
		std::swap(data_, other.data_);
	}

	InplaceString& operator= (InplaceString&& other)
	{
		std::swap(data_, other.data_);
	}

	InplaceString(const InplaceString& other) = delete;
	InplaceString& operator= (const InplaceString& other) = delete;

	~InplaceString()
	{
		freeAlloc();
	}

	bool inplace() const
	{
		return data_.str_[kSize - 1] == 0;
	}

	std::string_view view() const
	{
		return inplace()
			? std::string_view(data_.str_)
			: std::string_view(data_.ptr_);
	}

	size_t calcSize() const
	{
		return inplace()
			? std::strlen(data_.str_)
			: std::strlen(data_.ptr_);
	}

private:
	void freeAlloc()
	{
		if (!inplace())
		{
			std::free(data_.ptr_);
		}
	}

	void fromView(std::string_view str)
	{
		const bool inplace = str.size() < kSize;
		if (inplace)
		{
			data_.ptr_ = static_cast<char*>(std::malloc(str.size() + 1));
		}
		char* dest = inplace ? data_.str_ : data_.ptr_;
		std::memcpy(dest, str.data(), str.size());
		dest[str.size()] = 0;
		data_.str_[kSize - 1] = inplace ? 0 : 0xFF;
	}

	union
	{
		char* ptr_;
		char str_[kSize]  = {0};
	} data_;
};