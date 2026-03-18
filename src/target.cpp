#include <csignal>

#include <libkdebugger/target.hpp>
#include <libkdebugger/types.hpp>

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
}
