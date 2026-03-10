#pragma once

#include <unordered_map>

// I will implement this later... whole lotta constants lmao
#include <libkdebugger/detail/dwarf.h>

namespace kdebugger {
	
	class elf;

	class dwarf {
		
		const elf * m_Elf;

		std::unordered_map<std::size_t, std::unordered_map<std::uint64_t, abbrev>> m_AbbrevTables;

		public:
			dwarf(const elf & parent);
			const elf * elf_file() const { return m_Elf; }
	
			const std::unordered_map<std::uint64_t, abbrev> & get_abbrev_table(std::size_t offset);
	};
};
