#pragma once

// General includes
#include <memory>

// Project specific / private headers
#include <libkdebugger/elf.hpp>
#include <libkdebugger/process.hpp>
#include <libkdebugger/stack.hpp>

namespace kdebugger {
	class target {
		
		private:
			std::unique_ptr<process> m_Process;
			std::unique_ptr<process> m_Elf;
			stack m_Stack;

			elf_collection m_Elves;
			elf * m_MainElf;

			stoppoint_collection<breakpoint> m_Breakpoints();

			virt_addr dynamic_linker_rendezvous_address;

			void resolve_dynamic_linker_rendezvous();
			void resolve_dynamic_libraries();

			target(std::unique_ptr<process> proc, std::unique_ptr<elf> obj) 
				: m_Process {std::move(proc)}, m_Elf {std::move(obj)}, m_Stack{this} {}

			target(std::unique_ptr<process> proc, std::unique_ptr<elf> obj)
				: m_Process {std::move(proc)}, m_Stack {this}, m_MainElf {obj.get()},
					m_Elves.push(std::move(obj));

		public:
			target() = delete;
			target(const target &) = delete;
			target & operator=(const target &) = delete;

			static std::unique_ptr<target> launch(std::filesystem::path, std::optional<int> stdout_replacement = std::nullopt);
			static std::unique_ptr<target> attach(pid_t pid);

			process & get_process() {
				return *m_Process;
			}

			const process & get_process() const {
				return *m_Process;
			}

			elf & get_elf() {
				return *m_Elf;
			}

			const elf & get_elf() const {
				return *m_Elf;
			}

			void notify_stop(const kdebugger::stop_reason & reason);
	
			file_addr get_pc_file_address() const;
	
			stack & get_stack() {
				return m_Stack;
			}

			const stack & get_stack() const {
				return m_Stack;
			}

			struct find_functions_result {
				std::vector<die> dwarf_functions;
				std::vector<std::pair<const elf *, const Elf64_Sym *>> elf_functions;
			};

			breakpoint & create_address_breakpoint(virt_addr address, bool hardware = false, bool internal = false);
			breakpoint & created_function_breakpoint(std::string function_name, bool hardware = false, bool internal = false);
			breakpoint & create_line_breakpoint(std::filesystem::path file, std::size_t line, bool hardware = false, bool internal = false);

			stoppoint_collection<breakpoint> & breakpoints() {
				return m_Breakpoints;
			}

			const stoppoint_collection<breakpoint> & breakpoints() const {
				return m_Breakpoints;
			}

			find_functions_result find_functions(std::string name) const;

			kdebugger::stop_reason step_in();
			kdebugger::stop_reason step_out();
			kdebugger::stop_reason step_over();
	
			kdebugger::line_table::iterator line_entry_at_pc() const;
			kdebugger::stop_reason run_until_address(virt_addr address);

			std::string function_name_at_address(virt_addr address) const;
			std::optional<r_debug> read_dynamic_linker_rendezvous() const;
	
			elf_collection & get_elves() {
				return m_Elves;
			}

			const elf_collection & get_elves() const {
				return m_Elves;
			}

			elf & get_main_elf() {
				return *m_MainElf;
			}

			const elf & get_main_elf() const {
				return *m_MainElf;
			}
	};
}
