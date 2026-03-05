#pragma once

// general header includes
#include <filesystem>
#include <memory>
#include <sys/types.h>
#include <optional>
#include <vector>

// Private / project specific headers
#include <libkdebugger/registers.hpp>
#include <libkdebugger/types.hpp>
#include <libkdebugger/breakpoint_site.hpp>
#include <libkdebugger/stoppoint_collection.hpp>

// kdebugger::process::
namespace kdebugger {
	
	enum class process_state {
		stopped,
		running,
		exited,
		terminated
	};

	struct stop_reason {
		
		stop_reason(int wait_status);

		process_state reason;
		std::uint8_t info;
	};

	class process {
		
		private:	
			pid_t m_Pid {0};
			bool m_Terminate {true};
			bool m_Attached {true};
			process_state m_State {process_state::stopped};
			
			void read_all_registers() const;

			// holds the address of our registers
			std::unique_ptr<registers> m_Registers;

			// vector of unique break point sites
			// std::vector<std::unique_ptr<breakpoint_site>> m_BreakPointSites;

			// collection of stop points in the breakpoint site
			stoppoint_collection<breakpoint_site> m_BreakPointSites;

			process(pid_t pid, bool terminate, bool is_attached) : m_Pid {pid},
				m_Terminate {terminate}, m_Attached {is_attached},
			       		m_Registers {new registers(*this)} {}

            // private method for setting hardware breakpoints
            int set_hardware_stoppoint(virt_addr address, mode, std::size_t size);

		public:
			// delete default constructor
			process() = delete;

			/* registers extension --- */
			registers & get_registers() { return *m_Registers; }
			const registers & get_registers() { return *m_Registers; }
			
			// write to an equal sized register, given an offset
			void write_user_area(std::size_t offset, std::uint64_t data) const;
			
			// writing to all the GPRs and FPRs at once
			void write_fgprs(const user_fpregs_struct & fprs);
			void write_gprs(const user_regs_struct & gprs);

			// delete copy constructor
			process(const process &) = delete;

			// delete copy assignment
			process& operator=(const process &) = delete

			// launch debugger on a given process, return a static unique instance 
			static std::unique_ptr<process> launch(const std::filesystem::path path, 
					bool debug = true, 
					std::optional<int> stdout_replacement = std::nullopt);
			// attach to a currently running PID, return a static unique instance of ID
			static std::unique_ptr<process> attach(const pid_t pid);

			kdebugger::stop_reason step_instruction();
			kdebugger::stop_reason wait_on_signal() const;

			// reading and writing to and from memory
			std::vector<std::byte> read_memory(virt_addr address, std::size_t amount) const;
			// reads memory then fix a replacement for int3 instructions with original bytes
			std::vector<std::byte> read_memory_without_traps(virt_addr, std::size_t amount) const;
		
			void write_memory(virt_addr address, span<const std::byte> data);
			
			// read memory as a certain size
			template <class T> T read_memory_as(virt_addr address) const {
				auto data = read_memory(address, sizeof(T));
				return from_bytes<T>(data.data());
			}

			// resume the current process if halted
			void resume();

			pid_t pid() const {
				return m_Pid;
			}
	
			// helper function for getting the program counter
			virt_addr get_pc() const {
				return virt_addr {
					get_registers().read_by_id_as<std::uint64_t> (register_id::rip)
				};
			}

			// helper function for setting the program counter
			void set_pc(virt_addr address) {
				get_registers().write_by_id(register_id::rip, address.addr());
			}
			
            // public method for setting hardware breakpoints at a virtual address
            int set_hardware_breakpoint(breakpoint_site::id_type id, virt_addr address);

			// iterating over breakpoint sites - returns memory location
			breakpoint_site & create_breakpoint_site(virt_addr address, bool hardware = false, bool internal = false);
			
			// non-const and const overloads for breakpoint sites to return
			// the current breakpoint site
			stoppoint_collection<breakpoint_site> & breakpoint_sites() {
				return m_BreakPointSites;
			}

			stoppoint_collection<breakpoint_site> & breakpoint_sites() {
				return m_BreakPointSites;
			}

			~process();
	};
}
