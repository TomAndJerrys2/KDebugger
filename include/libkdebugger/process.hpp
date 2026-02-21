#pragma once

#include <filesystem>
#include <memory>
#include <sys/types.h>

// kdebugger::process::
namespace kdebugger {
	
	enum class process_state {
		stopped,
		running,
		exited,
		terminated
	};

	class process {
		
		pid_t m_Pid {0};
		bool m_Terminate {true};
		process_state m_State {process_state::stopped};

		public:
			// delete default constructor
			process() = delete;

			// delete copy constructor
			process(const process &) = delete;

			// delete copy assignment
			process& operator=(const process &) = delete

			// launch debugger on a given process, return a static unique instance 
			static std::unique_ptr<process> launch(const std::filesystem::path path);
			// attach to a currently running PID, return a static unique instance of ID
			static std::unique_ptr<process> attach(const pid_t pid);

			auto signal() const;
			
			// resume the current process if halted
			void resume();

			pid_t pid() const {
				return m_Pid;
			}

			~process();
	};
}
