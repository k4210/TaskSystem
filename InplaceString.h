#include <string_view>
#include <array>

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
		freeAlloc(); //TODO: try to reuse existing allocation

		fromView(str);
	}

	InplaceString(InplaceString&& other)
	{
		std::swap(data_.str_, other.data_.str_);
	}

	InplaceString& operator= (InplaceString&& other)
	{
		std::swap(data_.str_, other.data_.str_);
	}

	InplaceString(const InplaceString& other) = delete;
	InplaceString& operator= (const InplaceString& other) = delete;

	~InplaceString()
	{
		freeAlloc();
	}

	bool inplace() const
	{
		return data_.str_[kSize - 1] == '\0';
	}

	std::string_view view() const
	{
		return inplace()
			? std::string_view(data_.str_)
			: std::string_view(data_.ptr_);
	}

	size_t size() const
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
		if (str.size() < kSize)
		{
			std::memcpy(data_.str_, str.data(), str.size());
			data_.str_[str.size()] = '\0';
			data_.str_[kSize - 1] = 0; // Mark as inplace
		}
		else
		{
			data_.ptr_ = static_cast<char*>(std::malloc(str.size() + 1));
			std::memcpy(data_.ptr_, str.data(), str.size());
			data_.ptr_[str.size()] = '\0';
			data_.str_[kSize - 1] = 0xFF; // Mark as non-inplace
		}
	}

	union
	{
		char* ptr_;
		std::array<char, kSize> str_ = {0};
	} data_;
};