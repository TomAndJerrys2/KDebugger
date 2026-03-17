#include <libkdebugger/stack.hpp>
#include <libkdebugger/target.hpp>

std::vector<kdebugger::die> kdebugger::stack::inline_stack_at_pc() const {
	auto pc = m_Target->get_pc_file_address();
	
	if(!pc.elf_file())
		return {};

	return pc.elf_file().get_dwarf().inline_stack_at_address(pc);
}
