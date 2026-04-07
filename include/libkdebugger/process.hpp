#pragma once

// general header includes
#include <filesystem>
#include <memory>
#include <sys/types.h>
#include <optional>
#include <vector>
#include <unordered_map>
#include <csignal>

// Private / project specific headers
#include <libkdebugger/registers.hpp>
#include <libkdebugger/types.hpp>
#include <libkdebugger/breakpoint_site.hpp>
#include <libkdebugger/stoppoint_collection.hpp>

namespace kdebugger {

    struct syscall_information {
        std::uint16_t id;
        bool entry;

        union {
            std::array<std::uint64_t, 6> args;
            std::int64_t ret;
        };
    };

    // class for catchpoints invoked when a syscall
    // is encountered
    class syscall_catch_policy {
        
        private:
            syscall_catch_policy(mode mode, std::vector<int> to_catch) :
                m_Mode {mode}, m_toCatch {to_catch} {}

            mode m_Mode;
            std::vector<int> m_toCatch;

        public:
            enum mode {
                none,
                some,
                all
            };

            static syscall_catch_policy catch_all() {
                return {mode::all, {}};
            }

            static syscall_catch_policy catch_none() {
                return {mode::none, {}};
            }

            static syscall_catch_policy catch_some(std::vector<int> to_catch) {
                return {mode::some, std::move(to_catch)};
            }

            mode get_mode() const {
                return m_Mode;
            }

            const std::vector<int> & get_to_catch() const {
                return m_toCatch;
            }
    };
}

// kdebugger::process::
namespace kdebugger {
	
	enum class process_state {
		stopped,
		running,
		exited,
		terminated
	};

    enum class stoppoint_mode {
        write,
        read_write,
        execute
    };

    enum class trap_type {
        single_step,
        software_break,
        hardware_break,
        syscall,
	    clone,
    	unkown,
    };

	struct stop_reason {
		
	   stop_reason() = default;
       
       stop_reason(pid_t tid, int wait_status);

       stop_reason(process_state reason, std::uint8_t info, std::optional<trap_type> trap_reason = std::nullopt,
                std::optional<syscall_information> syscall_info = std::nullopt) : reason {reason}, info {info}, 
                    trap_reason {trap_reason}, syscall_info {syscall_info} {}

        bool is_step() const {
            return reason == process_state::stopped && info == SIGTRAP
                && trap_reason == trap_type::single_step;
        }

        bool is_breakpoint() const {
            return reason == process_state::stopped &&
                info == SIGTRAP && (trap_reason == trap_type::software_break
                        || trap_reason == trap_type::hardware_break);
        }

        std::uint8_t info;
		process_state reason;
        std::optional<trap_type> trap_reason;
	    std::optional<syscall_information> syscall_info;
        pit_t tid;
    };

    struct thread_state {
        pid_t tid;
        registers regs;
        stop_reason reason;
        process_state state = process_state::stopped;
    };

	class process {
		
		private:	
			pid_t m_Pid {0};
			bool m_Terminate {true};
			bool m_Attached {true};
			process_state m_State {process_state::stopped};

            bool expecting_syscall_exit {false};

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
            int set_hardware_stoppoint(virt_addr address, stoppoint_mode mode, std::size_t size);

            // watch point collections and breakpoint sites for watchpoints
            stoppoint_collection<breakpoint_site> m_BreakpointSites;
            stoppoint_collection<watchpoint> m_Watchpoints;

            // the stop reason because of a signal that was called
            void augment_stop_reason(stop_reason & reason);

            // reason for resuming if the syscall isnt one we have requested tracing for
            kdebugger::stop_reason mabye_resume_from_syscall(const stop_reason & reason);

            syscall_catch_policy m_SyscallCatchPolicy = syscall_catch_policy::catch_none();

            // target for inline stack tracking
            target * m_Target = nullptr;

            std::unordered_map<pid_t, thread_state> m_Threads;
            pid_t m_CurrentThread;

		public:

            std::unordered_map<int, std::uint64_t> get_auxv() const;

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
		
            // writes to memory using a span (pointer with a length)
			void write_memory(virt_addr address, span<const std::byte> data);
		
            // clears a hardware stoppoint
            void clear_hardware_stoppoint(int index);

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

            // evaluates the current location of a coressponding breakpoint or watchpoint
            std::variant<breakpoint_site::id_type, watchpoint::id_type> get_current_hardware_stoppoint() const;

			// iterating over breakpoint sites - returns memory location
			breakpoint_site & create_breakpoint_site(virt_addr address, bool hardware = false, bool internal = false);

            // sets a watchpoint at a given address with a size, id and type
            int set_watchpoint(watchpoint::id_type id, virt_addr address, stoppoint_mode mode, std::size_t size);
            
            // creates a watchpoint
            watchpoint & create_watchpoint(virt_addr address, stoppoint_mode mode, std::size_t size);

            // getter for return the watchpoints collection
            stoppoint_collection<watchpoint> & watchpoints() {
                return m_Watchpoints;
            }

            // const overload for returning watchpoints
            const stoppoint_collection<watchpoint> & watchpoint() const {
                return m_Watchpoints;
            }

			// non-const and const overloads for breakpoint sites to return
			// the current breakpoint site
			stoppoint_collection<breakpoint_site> & breakpoint_sites() {
				return m_BreakPointSites;
			}

			stoppoint_collection<breakpoint_site> & breakpoint_sites() {
				return m_BreakPointSites;
			}

            // sets the syscall catch policy
            void set_syscall_catch_policy(syscall_catch_policy info) {
                m_SyscallCatchPolicy = std::move(info);
            }

            void set_target(target * tgt) {
                m_Target = tgt;
            }

            breakpoint_site & create_breakpoint_site(breakpoint * parent, breakpoint_site::id_type id, virt_addr address, 
                    bool hardware = false, bool internal = false);

            void set_current_thread(pid_t tid) {
                m_CurrentThread = tid;
            }

            pid_t current_thread() const {
                return m_CurrentThread;
            }

            std::unordered_map<pid_t, thread_state> & thread_states() const {
                return m_Threads;
            }

			~process();
	};
}
