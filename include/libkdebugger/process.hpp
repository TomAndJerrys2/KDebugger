#pragma once

// general header includes
#include <filesystem>
#include <memory>
#include <sys/types.h>

// Private / project specific headers
#include <libkdebugger/registers.hpp>

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

			process(pid_t pid, bool terminate, bool is_attached) : m_Pid {pid},
				m_Terminate {terminate}, m_Attached {is_attached},
			       		m_Registers {new registers(*this)} {}
		
		public:
			// delete default constructor
			process() = delete;

			/* registers extension --- */
			registers & get_registers() { return *m_Registers; }
			const registers & get_registers() { return *m_Registers; }

			void write_user_area(std::size_t offset, std::uint64_t data) const;

			// delete copy constructor
			process(const process &) = delete;

			// delete copy assignment
			process& operator=(const process &) = delete

			// launch debugger on a given process, return a static unique instance 
			static std::unique_ptr<process> launch(const std::filesystem::path path, 
					bool debug = true;);
			// attach to a currently running PID, return a static unique instance of ID
			static std::unique_ptr<process> attach(const pid_t pid);

			stop_reason wait_on_signal() const;
			
			// resume the current process if halted
			void resume();

			pid_t pid() const {
				return m_Pid;
			}

			~process();
	};
}
