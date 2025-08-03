#pragma once

#include <type_traits>
#include <source_location>
#include <span>
#include <limits>
#include <cassert>
#include "Config.h"

#ifdef NDEBUG
#define DEBUG_CODE(x)
#define USE_DEBUG_CODE 0
#define LOCATION_PARAM
#define LOCATION_PARAM_IMPL 
#define LOCATION_PASS
#else
#define DEBUG_CODE(x) x
#define USE_DEBUG_CODE 1
#define LOCATION_PARAM , std::source_location location = std::source_location::current()
#define LOCATION_PARAM_IMPL , std::source_location location
#define LOCATION_PASS , location
#endif // NDEBUG

using uint8 = unsigned __int8;
using uint16 = unsigned __int16;
using int32 = __int32;
using uint32 = unsigned __int32;
using int64 = __int64;
using uint64 = unsigned __int64;

using Index = uint16;
constexpr Index kInvalidIndex = std::numeric_limits<Index>::max();

template<class EnumType>
constexpr bool enum_has_any(EnumType value, EnumType looking_for)
{
	static_assert(std::is_enum_v<EnumType>);
	using IntType = std::underlying_type_t<EnumType>;
	return !!(static_cast<IntType>(value) & static_cast<IntType>(looking_for));
}

template<class EnumType>
constexpr bool enum_has_all(EnumType value, EnumType looking_for)
{
	static_assert(std::is_enum_v<EnumType>);
	using IntType = std::underlying_type_t<EnumType>;
	const IntType to_check = static_cast<IntType>(looking_for);
	return (static_cast<IntType>(value) & to_check) == to_check;
}

template<class EnumType>
constexpr EnumType enum_or(EnumType a, EnumType b)
{
	static_assert(std::is_enum_v<EnumType>);
	using IntType = std::underlying_type_t<EnumType>;
	return static_cast<EnumType>(static_cast<IntType>(a) | static_cast<IntType>(b));
}

namespace ts
{
	template<class Type>
	class BaseIndex
	{
		Index value = kInvalidIndex;
	public:
		BaseIndex() = default;
		BaseIndex(const BaseIndex&) = default;
		BaseIndex(BaseIndex&&) = default;
		explicit BaseIndex(const Index raw_value)
			: value(raw_value)
		{}
		BaseIndex& operator= (const BaseIndex&) = default;
		BaseIndex& operator= (BaseIndex&&) = default;
		BaseIndex& operator= (const Index raw_value)
		{
			value = raw_value;
			return *this;
		}
		void Reset() { value = kInvalidIndex; }

		Index RawValue() const { return value; }
		bool IsValid() const { return value != kInvalidIndex; }

		bool operator == (const BaseIndex& other) const = default;
		bool operator != (const BaseIndex& other) const = default;

		bool operator == (const Index& other) const 
		{
			return value == other;
		}
		bool operator != (const Index& other) const 
		{
			return value != other;
		}
	};

	template<typename Node>
	auto GetPoolIndex(const Node& node)
	{
		const std::span<Node> nodes = Node::GetPoolSpan();
		const Node* first = nodes.data();
		const Index idx = static_cast<Index>(std::distance(first, &node));
		assert(idx < nodes.size());
		using IndexType = decltype(Node::next_);
		return IndexType{idx};
	}

	template<typename Node>
	Node& FromPoolIndex(const Index index)
	{
		std::span<Node> nodes = Node::GetPoolSpan();
		assert(index < nodes.size());
		return nodes[index];
	}

	template<typename Node>
	Node& FromPoolIndex(const BaseIndex<Node> index)
	{
		assert(index.IsValid());
		std::span<Node> nodes = Node::GetPoolSpan();
		assert(index.RawValue() < nodes.size());
		return nodes[index.RawValue()];
	}

	template<class Type, typename DataType>
	class BaseTag
	{
		DataType value = 0;
	public:
		static BaseTag FromRawValue(const DataType raw_value)
		{
			BaseTag result;
			result.value = raw_value;
			return result;
		}

		void Bump() { value++; }
		void Reset() { value = 0; }

		BaseTag Next() const { return FromRawValue(value + 1); }
		DataType RawValue() const { return value; }

		bool operator == (const BaseTag& other) const = default;
		bool operator != (const BaseTag& other) const = default;
	};
	

}