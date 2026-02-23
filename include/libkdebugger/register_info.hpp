#pragma once

#include <cstdint>
#include <cstddef>
#include <string_view>
#include <sys/user.h>

namespace kdebugger {
	// ID of a given 64bit register
	enum class register_id {
		#define DEFINE_REGISTER(name, dwarf_id, size, offset, size, format) name
		#include <libkdebugger/detail/registers.inc>
		#undef DEFINE_REGISTER
	};
	
	// type of register we are accessing
	enum class register_type {
		gpr, sub_grp, fpr, dr
	};

	// format or type the register can hold
	enum class register_format {
		uint, double_float, long_double, vector
	};
	
	// info about a given register
	struct register_info {
		register_id id;
		std::string_view name;
		std::int32_t dwarf_id;
		std::size_t size;
		std::size_t offset;

		register_type type;
		register_format fmt;
	};

	inline constexpr const register_info g_register_infos[] = {
		#define DEFINE_REGISTER (name, dwarf_id, size, offset, type, format) \
			{ register_id::name, #name, dwarf_id, size, offset, type, format}
		#include <libkdebugger/detail/registers.inc>
		#undef
	};
}
