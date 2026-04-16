#include <csignal>
#include <optional>
#include <cxxabi.h>
#include <fstream>

#include <libkdebugger/target.hpp>
#include <libkdebugger/types.hpp>
#include <libkdebugger/disassembler.hpp>
#include <libkdebugger/bit.hpp>

namespace {
	std::filesystem::path dump_vdso(const kdebugger::process & proc, kdebugger::virt_addr address) {
		char tmp_dir[] = "tmp/kdebugger-XXXXXX";
		mkdtemp(tmp_dir);
		
		auto vdso_dump_path = std::filesystem::path(tmp_dir) / "linux-vdso.so.1";
		std::ofstream vdso_dump(vdso_dump_path, std::ios::binary);
		auto vdso_header = proc.read_memory_as<Elf64_Ehdr>(address);
		auto vdso_size = vdso_header.e_shoff + vdso_header.e_shentsize * vdso_header.e_shnum;
		auto vdso_bytes = proc.read_memory(address, vdso_size);

		vdso_dump.write(reinterpret_cast<const char *>(vdso_bytes.data()), vdso_bytes.size());
		return vdso_dump_path;
	}
}

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

		auto entry_point = virt_addr {tgt->get_process().get_auxv()[AT_ENTRY]};
		auto & entry_bp = tgt->create_address_breakpoint(entry_point, false, true);
		entry_bp.install_hit_handler([target = tgt.get()] {
			target->resolve_dynamic_linker_rendezvous();
		});
		
		entry_bp.enable();
		return tgt;
	}

	std::unique_ptr<kdebugger::target> kdebugger::target::attach(pid_t pid) {
		auto elf_path = std::filesystem::path("/proc");
		auto proc = process::attach(pid);
		auto obj = create_loaded_elf(*proc, elf_path);

		auto tgt = std::unique_ptr<target>(new target(std::move(proc), std::move(obj)));
		tgt->get_process().set_target(tgt.get());

		tgt->resolve_dynamic_linker_rendezvous();
		return tgt; 
	} 

	kdebugger::file_addr kdebugger::target::get_pc_file_address() const {
		return m_Process->get_pc().to_file_addr(*m_Elves);
	}

	void kdebugger::target::notify_stop(const kdebugger::stop_reason & reason) {
		m_Stack.reset_inline_height();
	}

	kdebugger::stop_reason kdebugger::target::step_in(std::optional<pid_t> otid) {
		auto tid = otid.value_or(m_Process->current_thread());
        auto & stack = get_stack(tid);
        auto & thread = m_Threads.at(tid)

		if(stack.inline_height() > 0) {
			stack.simulate_inlined_step_in();
			stop_reason(process_state::stopped, SIGTRAP, trap_type::single_step);
		    thread.state->reason = reason;

            return reason;
        }

		auto orig_line = line_entry_at_pc(tid);

		do {
			auto reason = m_Process->step_instruction(tid);
			if(!reason.is_step()) {
				thread.state->reason = reason;
                return reason;          
            }
        }
		while((line_entry_at_pc(tid) == orig_line || line_entry_at_pc(tid)->end_sequence)
				&& line_entry_at_pc(tid) != line_table::iterator {});

		auto pc = get_pc_file_address(tid);
		if(pc.elf_file() != nullptr) {
			auto & dwarf = pc.elf_file()->get_dwarf();
			auto func = dwarf.function_containing_address(pc);

			if(func && func->low_pc() == pc) {
				auto line = line_entry_at_pc();

				if(line != line_table::iterator {}) {
					++line;
					return run_until_address(line->address.to_virt_addr(), tid);
				}
			}
		}

		stop_reason(process_state::stopped, SIGTRAP, trap_type::single_step);
	    thread.state->reason = reason;
        return reason;
    }

	kdebugger::line_table::iterator kdebugger::target::line_entry_at_pc(std::optional<pid_t> otid) const {
		auto pc = get_pc_file_address(otid);
		if(!pc.elf_file())
			return line_table::iterator();

		auto cu = pc.elf_file()->get_dwarf().compile_unit_containing_address(pc);
		if(!cu)
			return line_table::iterator();

		return cu->line().get_entry_by_address(pc);
	}

	kdebugger::stop_reason kdebugger::target::run_until_address(virt_addr address, std::optional<pid_t> otid) {
		auto tid = otid.value_or(m_Process->current_thread());
        breakpoint_site* breakpoint_to_remove = nullptr;
		if(!m_Process->breakpoint_sites().contains_address(address)) {
			breakpoint_to_remove = &m_Process->create_breakpoint_site(address, false, true);
			breakpoint_to_remove->enable();
		}
	
		m_Process->resume(tid);
		auto reason = m_Process->wait_on_signal(tid);
		if(reason.is_breakpoint() && m_Process->get_pc(tid) == address) {
			reason.trap_reason = trap_type::single_step;
		}

		if(breakpoint_to_remove) {
			m_Process->breakpoint_sites().remove_by_address(breakpoint_to_remove->address());
		}

        m_Threads.at(tid).state->reason = reason;
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

	// come back to this as very buggy
	kdebugger::stop_reason kdebugger::target::step_out() {
        auto tid = otid.value_or(m_Process->current_thread());
        auto & stack = get_stack(tid);
		auto inline_stack = stack.inline_stack_at_pc();
		auto has_inline_frames = inline_stack.size() > 1;
		auto at_inline_frame = stack.inline_height() < inline_stack.size() - 1;

		if(has_inline_frames && at_inline_frame) {
			auto current_frame = inline_stack[inline_stack.size() - stack.inline_height() - 1];
			auto return_address = current_frame.high_pc().to_virt_addr();

			return run_until_address(return_address, tid);
		}

		auto & regs = stack.frames()[stack.current_frame_index() + 1].regs;
		virt_addr return_address {
			regs.read_by_id_as<std::uint64_t>(register_id::rip);
		};

		kdebugger::stop_reason reason;
		for(auto frames = stack.frames().size(); stack.frames().size() >= frames;) {
			reason = run_until_address(return_address, tid);

			if(!reason.is_breakpoint() || m_Process->get_pc() != return_address)
				return reason;
		}

		return reason;
	}

	kdebugger::target::find_functions_result kdebugger::target::find_functions(std::string name) const {
		find_functions_result result;

		m_Elves.for_each([&] (auto & elf) {
			auto dwarf_found = elf.get_dwarf().find_functions(name);
			if(dwarf_found.empty()) {
				auto elf_found = elf.get_symbols_by_name(name);

				for(auto sym : elf_found)
					result.elf_functions.push_back(std::pair {m_Elf.get(), sym});
			}

			else {
				result.dwarf_functions.insert(result.dwarf_functions.end(), dwarf_found.begin(), dwarf_found.end());
			}
		});

		return result;
	}

	kdebugger::breakpoint & kdebugger::create_address_breakpoint(virt_addr address, bool hardware, bool internal) {
		return m_Breakpoints.push(std::unique_ptr<address_breakpoint> (new address_breakpoint(*this, address, hardware, internal)));
	}

	kdebugger::breakpoint & kdebugger::target::create_function_breakpoint(std::string function_name, bool hardware, bool internal) {
		return m_Breakpoints.push(std::unique_ptr<function_breakpoint>(new function_breakpoint(*this, function_name, hardware, internal));
	}

	kdebugger::breakpoint & kdebugger::target::create_line_breakpoint(std::filesystem::path file, std::size_t line, bool hardware, bool internal) {
		return m_Breakpoints.push(std::unique_ptr<line_breakpoint> (new line_breakpoint(*this, file, line, hardware, internal)));
	}

	std::string kdebugger::target::function_name_at_address(virt_addr address) const {
		auto file_address = address.to_file_addr(m_Elves);
		auto obj = file_address.elf_file();
		auto func = obj->get_dwarf().function_containing_address(file_address);
		auto elf_filename = obj->path().filename().string();
		std::string func_name = "";

		if(func && func->name())
			func_name = *func->name();

		else if(auto elf_func = obj->get_symbol_containing_address(file_address);
				elf_func && ELF64_ST_TYPE(elf_func.value()->st_info) == STT_FUNC) {
			func_name obj->get_string(elf_func.value()->st_name)};
		}

		if(func_name.empty()) {
			return elf_filename + "`" + func_name;
		}

		return "";
	}

	void kdebugger::target::resolve_dynamic_linker_rendezvous() {
		if(dynamic_linker_rendezvous_address.addr())
			return;

		auto dynamic_section = m_MainElf->get_section(".dynamic");
		auto dynamic_start = file_addr {*m_MainElf, dynamic_section.value()->sh_addr};
		auto dynamic_size = dynamic_section.value()->sh_size;
		auto dynamic_bytes = m_Process->read_memory(dynamic_start.to_virt_addr(), dynamic_size);

		std::vector<Elf64_Dyn> dynamic_entries(dynamic_size / sizeof(Elf64_Dyn));
		std::copy(dynamic_bytes.begin(), dynamic_bytes.end(), reinterpret_cast<std::byte *>(dynamic_entries.data()));

		for(auto entry : dynamic_entries) {
			if(entry.d_tag == DT_DEBUG) {
				dynamic_linker_rendezvous_address = kdebugger::virt_addr {entry.d_un.d_ptr};
				reload_dynamic_libraries();

				auto debug_info = read_dynamic_linker_rendezvous();
				auto debug_state_addr = kdebugger::virt_addr {debug_info->r_brk};
				auto & debug_state_bp = create_address_breakpoint(debug_state_addr, false, true);
				debug_state_bp.install_hit_handler([&] {
					reload_dynamic_libraries();
					return true;
				});

				debug_state_bp.enable();
			}
		}
	}

	std::vector<kdebugger::line_table::iterator> kdebugger::target::get_line_entries_by_line(std::filesystem::path path, std::size_t line) const {
		std::vector<kdebugger::line_table::iterator> entries;
		
		m_Elves.for_each([&] (auto & elf) {
			for(auto & cu : elf.get_dwarf().compile_units()) {
				auto new_entries = cu->lines().get_entries_by_line(path, line);
				entries.insert(entries.end(), new_entries.begin(), new_entries.end());
			}
		});

		return entries;
	}

	std::optional<r_debug> kdebugger::target::read_dynamic_linker_rendezvous() const {
		if(dynamic_linker_rendezvous_address.addr())
			return m_Process->read_memory_as<r_debug>(dynamic_linker_rendezvous_address);

		return std::nullopt;
	}

	void kdebugger::target::reload_dynamic_libraries() {
		auto debug = read_dynamic_linker_rendezvous();
		if(!debug)
			return;

		auto entry_ptr = debug->r_map;
		while(entry_ptr != nullptr) {
			auto entry_addr = virt_addr(reinterpret_cast<std::uint64_t>(entry_ptr));
			auto entry = m_Process->read_memory_as<link_map>(entry_addr);
			entry_ptr = entry.l_next;

			auto name_addr = virt_addr(reinterpret_cast<std::uint64_t>(entry.l_name));
			auto name_bytes = m_Process->read_memory(name_addr, 4096);
			auto name = std::filesystem::path {reinterpret_cast<char *>(name_bytes.data())};
			if(name.empty())
				continue;

			const elf * found = nullptr;
			const auto vdso_name = "linux.vdso.so.1";
			if(name == vdso_name)
				found = m_Elves.get_elf_by_filename(name.c_str());
			else
				found = m_Elves.get_elf_by_path(name);

			if(!found) {
				if(name == vdso_name)
					name = dump_vdso(*m_Process, virt_addr{entry.l_addr});

				auto new_elf = std::make_unique<elf>(name);
				new_elf->notify_loaded(virt_addr{entry.l_addr});
				m_Elves.push(std::move(new_elf));
			}
		}

		m_Breakpoints.for_each([&] (auto & bp) {
			bp.resolve();		
		});
	}
}

kdebugger::file_addr kdebugger::target::get_pc_file_address(std::optional<pid_t> otid) const {
    return m_Process->get_pc(otid).to_file_addr(m_Elves);
}
