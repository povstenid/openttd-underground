/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file format.hpp String formatting functions and helpers. */

#ifndef FORMAT_HPP
#define FORMAT_HPP

#include "../3rdparty/fmt/format.h"
#include "convertible_through_base.hpp"

template <typename T, bool IsEnum = std::is_enum_v<T>>
struct openttd_fmt_base_type;

template <typename T>
struct openttd_fmt_base_type<T, true> {
	using type = std::underlying_type_t<T>;

	static constexpr type Convert(const T &value)
	{
		return static_cast<type>(value);
	}
};

template <typename T>
	requires (!std::is_enum_v<T> && ConvertibleThroughBase<T>)
struct openttd_fmt_base_type<T, false> {
	using type = typename T::BaseType;

	static constexpr type Convert(const T &value)
	{
		return value.base();
	}
};

template <typename T, typename Char> requires (std::is_enum_v<T> || ConvertibleThroughBase<T>)
struct fmt::formatter<T, Char> : fmt::formatter<typename openttd_fmt_base_type<T>::type, Char> {
	using underlying_type = typename openttd_fmt_base_type<T>::type;
	using parent = typename fmt::formatter<underlying_type, Char>;

	constexpr fmt::format_parse_context::iterator parse(fmt::format_parse_context &ctx)
	{
		return parent::parse(ctx);
	}

	fmt::format_context::iterator format(const T &value, fmt::format_context &ctx) const
	{
		return parent::format(openttd_fmt_base_type<T>::Convert(value), ctx);
	}
};

template <class... Args>
void format_append(std::string &out, fmt::format_string<Args...> &&fmt, Args&&... args)
{
	fmt::format_to(std::back_inserter(out), std::forward<decltype(fmt)>(fmt), std::forward<decltype(args)>(args)...);
}

#endif /* FORMAT_HPP */
