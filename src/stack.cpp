#include <libkdebugger/stack.hpp>
#include <libkdebugger/target.hpp>

std::vector<kdebugger::die> kdebugger::stack::inline_stack_at_pc() const {
	auto pc = m_Target->get_pc_file_address();
	
	if(!pc.elf_file())
		return {};

	return pc.elf_file().get_dwarf().inline_stack_at_address(pc);
}

void kdebugger::stack::reset_inline_height() {
	auto stack = inline_stack_at_pc();
	m_InlineHeight = 0;

	auto pc = m_Target->get_pc_file_address();
	for(auto it = stack.rbegin(); it != stack.rend() && it->low_pc == pc; ++it)
		++m_InlineHeight;
}

kdebugger::span<const kdebugger::stack_frame> kdebugger::stack::frames() const {
	return {m_Frames.data() + m_InlineHeight, m_Frames.size() - m_InlineHeight};
}

const kdebugger::registers & kdebugger::stack::regs() const {
	return m_Frames[m_CurrentFrame].regs;
}

kdebugger::virt_addr kdebugger::stack::get_pc() const {
	return virt_addr {regs().read_by_id_as<std::uint64_t>(kdebugger::register_id::rip)};
}

void kdebugger::target::notify_stop(const kdebugger::stop_reason & reason) {
	m_Stack.unwind();
}


