#include <csignal>
#include <optional>

#include <libkdebugger/target.hpp>
#include <libkdebugger/types.hpp>
#include <libkdebugger/disassembler.hpp>
#include <libkdebugger/bit.hpp>

namespace {

	std::unique_ptr<kdebugger::elf> create_loaded_elf(const kdebugger::process & proc, 
			const std::filesystem::path & path) {
		auto auxv = proc.get_auxv();
		auto obj = std::make_unique<kdebugger::elf>(path);

		obj->notify_loaded(kdebugger::virt_addr(auxv[AT_ENTRY] - obj->get_header().e_entry));
	
		return obj;
	}	

	std::unique_ptr<kdebugger::target> kdebugger::target::launch(std::filesystem::path path, std::optional<int> stdout_replacement) {
		auto proc = process::launch(path, true, stdout_replacement);
		auto obj = create_loaded_elf(*proc, path);

		auto tgt = std::unique_ptr<target>(new target(std::move(proc), std::move(obj)));
		tgt->get_process().set_target(tgt.get());
		return tgt;
	}

	std::unique_ptr<kdebugger::target> kdebugger::target::attach(pid_t pid) {
		auto elf_path = std::filesystem::path("/proc");
		auto proc = process::attach(pid);
		auto obj = create_loaded_elf(*proc, elf_path);

		auto tgt = std::unique_ptr<target>(new target(std::move(proc), std::move(obj)));
		tgt->get_process().set_target(tgt.get());
		return tgt; 
	} 

	kdebugger::file_addr kdebugger::target::get_pc_file_address() const {
		return m_Process->get_pc().to_file_addr(*m_Elf);
	}

	void kdebugger::target::notify_stop(const kdebugger::stop_reason & reason) {
		m_Stack.reset_inline_height();
	}

	kdebugger::stop_reason kdebugger::target::step_in() {
		auto & stack = get_stack();
		if(stack.inline_height() > 0) {
			stack.simulate_inlined_step_in();
			return stop_reason(process_state::stopped, SIGTRAP, trap_type::single_step);
		}

		auto orig_line = line_entry_at_pc();

		do {
			auto reason = m_Process->step_instruction();
			if(!reason.is_step())
				return reason;
		} 
		while((line_entry_at_pc() == orig_line || line_entry_at_pc()->end_sequence)
				&& line_entry_at_pc() != line_table::iterator {});

		auto pc = get_pc_file_address();
		if(pc.elf_file() != nullptr) {
			auto & dwarf = pc.elf_file()->get_dwarf();
			auto func = dwarf.function_containing_address(pc);

			if(func && func->low_pc() == pc) {
				auto line = line_entry_at_pc();

				if(line != line_table::iterator {}) {
					++line;
					return run_until_address(line->address.to_virt_addr());
				}
			}
		}

		return stop_reason(process_state::stopped, SIGTRAP, trap_type::single_step);
	}

	kdebugger::line_table::iterator kdebugger::target::line_entry_at_pc() const {
		auto pc = get_pc_file_address();
		if(!pc.elf_file())
			return line_table::iterator();

		auto cu = pc.elf_file()->get_dwarf().compile_unit_containing_address(pc);
		if(!cu)
			return line_table::iterator();

		return cu->line().get_entry_by_address(pc);
	}

	kdebugger::stop_reason kdebugger::target::run_until_address(virt_addr address) {
		breakpoint_site* breakpoint_to_remove = nullptr;
		if(!m_Process->breakpoint_sites().contains_address(address)) {
			breakpoint_to_remove = &m_Process->create_breakpoint_site(address, false, true);
			breakpoint_to_remove->enable();
		}
	

		m_Process->resume();
		auto reason = m_Process->wait_on_signal();
		if(reason.is_breakpoint() && m_Process->get_pc() == address) {
			reason.trap_reason = trap_type::single_step;
		}

		if(breakpoint_to_remove) {
			m_Process->breakpoint_sites().remove_by_address(breakpoint_to_remove->address());
		}

		return reason;
	}

	kdebugger::stop_reason kdebugger::target::step_over() {
		auto orig_line = line_entry_at_pc();
		disassembler disas(*m_Process);
		kdebugger::stop_reason reason;

		auto & stack = get_stack();
		do {
			auto inline_stack = stack.inline_stack_at_pc();
			auto at_start_of_inline_form = stack.inline_height() > 0;

			if(at_stack_of_inline_frame) {
				auto frame_to_skip = inline_skip[inline_stack.size() - stack.inline_height()];
				auto return_address = frame_to_skip.high_pc().to_virt_addr();
				reason = run_until_address(return_address);

				if(!reason.is_step() || m_Process->get_pc() != return_address)
					return reason;
			}

			else if(auto instructions = disas.disassemble(2, m_Process->get_pc()); instructions[0].text.rfind("call") == 0) {
				reason = run_until_address(instructions[1].address);

				if(!reason.is_step() || m_Process->get_pc() != instructions[1].address)
					return reason;
			}

			else {
				reason = m_Process->step_instruction();
				if(!reason.is_step())
					return reason;
			}
		}
		while((line_entry_at_pc() == orig_line || line_entry_at_pc()->end_sequence) &&
					line_entry_at_pc() != line_table::iterator {});

		return reason;
	}

	kdebugger::stop_reason kdebugger::target::step_out() {
		auto & stack = get_stack();
		auto inline_stack = stack.inline_stack_at_pc();
		auto has_inline_frames = inline_stack.size() > 1;
		auto at_inline_frame = stack.inline_height() < inline_stack.size() - 1;

		if(has_inline_frames && at_inline_frame) {
			auto current_frame = inline_stack[inline_stack.size() - stack.inline_height() - 1];
			auto return_address = current_frame.high_pc().to_virt_addr();

			return run_until_address(return_address);
		}

		auto frame_pointer = m_Process->get_registers().read_by_id_as<std::uint64_t>(register_id::rbp);

		auto return_address = m_Process->read_memory_as<std::uint64_t>(virt_addr {frame_pointer + 8});

		return run_until_address(virt_addr {return_address});
	}
}
