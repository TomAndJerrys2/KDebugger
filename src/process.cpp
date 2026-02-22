// General Headers
#include <sys/ptrace.h>
#include <sys/types.h>
#include <unistd.h>

// Private / Project-specific headers
#include <libkdebugger/process.hpp>
#include <libkdebugger/error.hpp>
#include <libkdebugger/pipe.hpp>

namespace {
	
	void exit_with_perror(kdebugger::pipe & channel, std::string & prefix) {
		auto message = prefix + ": " + std::strerror(errno);

		channel.write(
			reinterpret_cast<std::byte *> (message.data(), message.size());		
		);

		exit(-1);
	}
}

// launches a given process via a path
std::unique_ptr<kdebugger::process> kdebugger::process::launch(const std::filesystem::path path) {
	// set close_on_exec to be true --> pipe.hpp/pipe.cpp
	kdebugger::pipe channel(true)

	pid_t pid {};
	if((pid = fork()) < 0) {
		error::send_errno("fork failed");
	}

	if(pid == 0) {
		
		channel.close_read();
		if(ptrace(PTRACE_TRACEME, 0, nullptr, nullptr) < 0) {
			exit_with_perror(channel, "Tracing failed");
		}

		if(execlp(path.c_str(), path.c_str(), nullptr) < 0) {
			exit_with_perror(channel, "exec failed");
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

// resumes a process being held
void sdb::process::resume() {
	
	if(ptrace(PTRACE_CONT, m_Pid, nullptr, nullptr) < 0) {
		error::send_errno("Could not resume Process");
	}

	m_State = process_state::running;
}

// produce a stop reason for why the process
// was stopped by an exit code or signal
kdebugger::stop_reason::stop_reason(int wait_status) {
	
	if(WIFEXITED(wait_status)) {
		reason = process_state::exited;
		info = WEXITSTATUS(wait_status);
	}

	else if(WIFSIGNAL(wait_status)) {
		reason = process_state::terminated;
		info = WTERMSIG(wait_status);		
	}

	else if(WIFSTOPPED(wait_status)) {
		reason = process_state::stopped;
		info = WSTOPSIG(wait_status);
	}
}

kdebugger::stop_reason kdebugger::process::wait_on_signal() {
	
	int wait_status;
	int options {0};

	if(waitpid(m_Pid, &wait_status, options) < 0) {
		error::send_errno("waitpid failed");
	}

	stop_reason reason(wait_status);
	m_State = reason.reason;
	return reason;
}

// destructor call
kdebugger::process::~process() {

	if(m_Pid != 0) {
		int status {};
		
		if(m_State == process_state::running) {
			kill(m_Pid, SIGSTOP);
			waitpid(m_Pid, &status, 0);
		}

		ptrace(PTRACE_DETACH, m_Pid, nullptr, nullptr);
		kill(m_Pid, SIGCONT);

		if(m_Terminate) {
			kill(m_Pid, SIGKILL);
			waitpid(m_Pid, &status, 0);
		}
	}
}
