#include <libkdebugger/dwarf.hpp>

const std::unordered_map<std::uint64_t, kdebugger::abbrev> & kdebugger::dwarf::get_abbrev_table(std::size_t offset) {
	if(!m_AbbrevTables.count(offset))
		m_AbbrevTables.emplace(offset, parse_abbrev_table(*m_Elf, offset));

	return m_AbbrevTables.at(offset);
}
