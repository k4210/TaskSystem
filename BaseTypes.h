#pragma once

#include <type_traits>

using uint8 = unsigned __int8;
using uint16 = unsigned __int16;
using int32 = __int32;
using uint32 = unsigned __int32;

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