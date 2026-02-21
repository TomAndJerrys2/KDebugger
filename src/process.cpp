// General Headers
#include <sys/ptrace.h>
#include <sys/types.h>
#include <unistd.h>

// Private / Project-specific headers
#include <libkdebugger/process.hpp>
#include <libkdebugger/error.hpp>

// launches a given process via a path
std::unique_ptr<kdebugger::process> kdebugger::process::launch(const std::filesystem::path path) {
	
	pid_t pid {};
	if((pid = fork()) < 0) {
		error::send_errno("fork failed");
	}

	if(pid == 0) {
		
		if(ptrace(PTRACE_TRACEME, 0, nullptr, nullptr) < 0) {
			error::send_errno("Tracing failed");
		}

		if(execlp(path.c_str(), path.c_str(), nullptr) < 0) {
			error::send_errno("exec failed");
		}
	}

	std::unique_ptr<process> proc = new process(pid, true);
	proc->wait_on_signal();

	return proc;
}

// attaches to an already running process via its PID
std::unique_ptr<kdebugger::process> kdebugger::process::attach(const pid_t pid) {
	
	if(pid == 0) {
		error::send("Invalid PID");
	}

	if(ptrace(PTRACE_ATTACH, pid, nullptr, nullptr) < 0) {
		error::send_errno("Could not attach");
	}

	std::unique_ptr<process> proc = new process(pid, false);
	proc->wait_on_signal;

	return proc;
}
