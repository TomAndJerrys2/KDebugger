// Private / Project specific headers
#include <libkdebugger/types.hpp>
#include <libkdebugger/elf.hpp>

// general include paths
#include <cassert>

kdebugger::virt_addr kdebugger::file_addr::to_virt_addr() const {
	assert(m_Elf && "to_virt_addr called on null address");

	auto section = m_Elf->get_section_containing_address(*this);
	if(!section)
		return virt_addr{};

	return virt_addr { m_Addr + m_Elf->load_bias().addr() };
}

kdebugger::file_addr kdebugger::virt_addr::to_file_addr(const elf & obj) const {
	auto section = obj.get_section_containing_address(*this);
	if(!section)
		return file_addr{};

	return file_addr { obj, m_Addr - obj.load_bias().addr() };
}

kdebugger::file_addr kdebugger::virt_addr::to_file_addr(const elf_collection & elves) const {
	auto obj = elves.get_elf_containing_address(*this);
	if(!obj)
		return file_addr{};

	return to_file_addr(*obj);
}
