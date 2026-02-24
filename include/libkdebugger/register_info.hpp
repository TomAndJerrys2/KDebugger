#pragma once

#include <cstdint>
#include <cstddef>
#include <string_view>
#include <sys/user.h>

namespace kdebugger {
	// ID of a given 64bit register -> detail/registers.inc
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
	
	// information about a given type of register -> detail/registers.inc
	inline constexpr const register_info g_register_infos[] = {
		#define DEFINE_REGISTER (name, dwarf_id, size, offset, type, format) \
			{ register_id::name, #name, dwarf_id, size, offset, type, format}
		#include <libkdebugger/detail/registers.inc>
		#undef
	};

	template<class F> const register_info & register_info_by(F f) {
		auto it = std::find_if(
				std::begin(g_register_infos), 
				std::end(g_register_infos),
				f				
		);

		if(it == std::end(g_register_infos))
			error::send("Cannot find register info");

		return *it;
	}	
	
	// get a register by its id
	inline const register_info & register_by_id(register_id id) {
		return register_info_by([id] (auto & i) {return i.id == id; });
	}

	// get a register by its name
	inline const register_info & register_info_by_name(std::string_view name) {
		return register_info_by([name] (auto & i) { return i.name == name; });
	}

	// get a register by its DWARF id
	inline const register_info & register_info_by_dwarf(std::int32_t dwarf_id) {
		return register_info_by([dwarf_id] (auto & i) { return i.dwarf_id == dwarf_id; });
	}
}
