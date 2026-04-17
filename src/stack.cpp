#include <libkdebugger/stack.hpp>
#include <libkdebugger/target.hpp>

std::vector<kdebugger::die> kdebugger::stack::inline_stack_at_pc() const {
	auto pc = m_Target->get_pc_file_address(m_Tid);
	
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

void kdebugger::stack::unwind() {
	reset_inline_height();
	m_CurrentFrame = m_InlineHeight;

	auto virt_pc = m_Target->get_process().get_pc(m_Tid);
	auto file_pc = m_Target->get_pc_file_address(m_Tid);
	auto & proc = m_Target->get_process();
	auto regs = proc.get_registers(m_Tid);

	m_Frames.clear();

	auto elf = file_pc.elf_file();
	if(!elf)
		return;

	// creates stack frame objects and unwinds the next
	while(virt_pc.addr() != 0 && elf == &m_Target->get_elf()) {
		auto & dwarf = elf->get_dwarf();
		auto inline_stack = dwarf.inline_stack_at_address(file_pc);
		if(inline_stack.empty())
			return;

		if(inline_stack.size() > 1) {
			create_base_frame(regs, inline_stack, file_pc, true);
			create_inline_stack_frames(regs, inline_stack, file_pc);
		}

		else {
			create_base_frame(regs, inline_stack, file_pc, false);
		}

		regs = dwarf.cfi().unwind(proc, file_pc, m_Frames.back().regs);
		virt_pc = virt_addr {
			regs.read_by_id_as<std::uint64_t>(register_id::rip) - 1;
		};

		file_pc = virt_pc.to_file_addr(m_Target->get_elves());
		elf = file_pc.elf_file();
	}
}

void kdebugger::stack::create_base_frame(const registers & regs, const std::vector<kdebugger::die> inline_stack, file_addr pc, bool inlined) {
	auto backtrace_pc = pc.to_virt_addr();
	auto line_entry = pc.elf_file()->get_dwarf().line_entry_at_address(pc);
	if(line_entry != line_table::iterator{})
		backtrace_pc = line_entry->address.to_virt_addr();

	m_Frames.push_back({regs, backtrace_pc, inline_stack.back(), inlined});
	m_Frames.back().location = source_location(line_entry->file_entry, line_entry->line);
}

void kdebugger::stack::create_inline_stack_frames(const registers & regs, const std::vector<kdebugger::die> inline_stack, file_addr pc) {
	for(auto it = inline_stack.rbegin() + 1; it != inline_stack.rend(); ++it) {
		auto inlined_pc = std::prev(it)->low_pc().to_virt_addr();
		m_Frames.push_back(stack_frame{regs, inlined_pc, *it});
		m_Frames.back().inlined = std::next(it) != inline_stack.rend();
		m_Frames.back().location = std::prev(it)->location();
	}
}
