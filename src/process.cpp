// General Headers
#include <sys/ptrace.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/personality>
#include <sys/uio.h>

// Private / Project-specific headers
#include <libkdebugger/process.hpp>
#include <libkdebugger/error.hpp>
#include <libkdebugger/pipe.hpp>
#include <libkdebugger/bit.hpp>

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
std::unique_ptr<process> process::launch(const std::filesystem::path path, 
				bool debug, std::optional<int> stdout_replacement) {
	// set close_on_exec to be true --> pipe.hpp/pipe.cpp
	pipe channel(true)

	pid_t pid {};
	if((pid = fork()) < 0) {
		error::send_errno("fork failed");
	}

	if(pid == 0) {
		
		personality(ADDR_NO_RANDOMIZE);
		channel.close_read();

		if(stdout_replacement()) {
			if(dup2(*stdout_replacement, STDOUT_FILENO) < 0)
				exit_with_perror(channel, "stdout replacement failed");
		}

		if(debug && ptrace(PTRACE_TRACEME, 0, nullptr, nullptr) < 0) {
			exit_with_perror(channel, "Tracing failed");
		}

		if(execlp(path.c_str(), path.c_str(), nullptr) < 0) {
			exit_with_perror(channel, "exec failed");
		}
	}
	
	channel.close_write();
	auto data = channel.read();
	channel.close_read();

	if(data.size() > 0) {
		waitpid(pid, nullptr, 0);
		auto chars = reinterpret_cast<char *> (data.data());

		error::send(std::string(chars, chars + data.size()));
	}

	std::unique_ptr<process> proc = new process(pid, true);
	
	if(debug)
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

	std::unique_ptr<process> proc = new process(pid, false, true);
	proc->wait_on_signal;

	return proc;
}

// resumes a process being held
void sdb::process::resume() {
	
	auto pc = get_pc();
	if(m_BreakPointSites.enable_stoppoint_at_address(pc)) {
		auto & bp = m_BreakPoinSites.get_by_address(pc);
		bp.disable();

		if(ptrace(PTRACE_SINGLESTEP, m_Pid, nullptr, nullptr) < 0)
			error::send_errno("Failed to single-step!");

		int wait_status;
		if(waitpid(m_Pid, &wait_status, 0) < 0)
			error::send_errno("waitpid failed");

		bp.enable();
	}

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

// waits for a signal to be passed - passes a reason back
// to our state object in case of exception 
kdebugger::stop_reason kdebugger::process::wait_on_signal() {
	
	int wait_status;
	int options {0};

	if(waitpid(m_Pid, &wait_status, options) < 0) {
		error::send_errno("waitpid failed");
	}

	stop_reason reason(wait_status);
	m_State = reason.reason;

	if(m_Attached && m_State == process_state::stopped) {
		read_all_registers();
		
		auto instr_begin = get_pc() - 1;
		if(reason.info == SIGTRAP && m_BreakPointSites.enabled_stoppoint_at_address(instr_begin))
			set_pc(instr_begin)
	}

	return reason;
}

// destructor call
kdebugger::process::~process() {

	if(m_Pid != 0) {
		int status {};
		
		if(m_Attached) {
			if(m_State == process_state::running) {
				kill(m_Pid, SIGSTOP);
				waitpid(m_Pid, &status, 0);
			}

			ptrace(PTRACE_DETACH, m_Pid, nullptr, nullptr);
			kill(m_Pid, SIGCONT);
		} 

		if(m_Terminate) {
			kill(m_Pid, SIGKILL);
			waitpid(m_Pid, &status, 0);
		}
	}
}

// register read all values
void kdebugger::process::read_all_registers() const {
	
	if(ptrace(PTRACE_GETREGS, m_Pid, nullptr, &get_registers().m_Data.i387) < 0)
		error::send_errno("Could not read GPR registers");

	if(ptrace(PTRACE_GETFPREGS, m_Pid, nullptr, &get_registers().m_Data.i387) < 0)
		error::send_errno("Could not read FPR registers");

	for(size_t i {0}; i < 8; ++i) {
		// reads debug registers
		auto id = static_cast<int> (register_id::dr0) + i;
		auto info = register_info_by_id(static_cast<register_id> (id));

		errno = 0;
		std::uint64_t data = ptrace(PTRACE_PEEKUSER, m_Pid, info.offset, nullptr);

		if(errno != 0)
			error::send_errno("Could not read debug register");

		get_registers().m_Data.u_debugreg[i] = data;
	}
}	

// write to a register given a similar user area
void kdebugger::process::write_user_area(std::size_t offset, std::uint64_t data) {
	
	if(ptrace(PTRACE_POKEUSER, m_Pid, offset, data) < 0)
		error::send_errno("Could not write to user area");
}

// write to all the fprs in the x87 space
void kdebugger::process::write_fprs(const user_fpregs_struct & fprs) {
	
	if(ptrace(PTRACE_SETFPREGS, m_Pid, nullptr, &fprs) < 0)
		error::send_errno("Could not write floating point registers");
}

void kdebugger::process::write_gprs(const user_regs_struct & gprs) {
	
	if(ptrace(PTRACE_SETREGS, m_Pid, nullptr, &gprs) < 0)
		error::send_errno("Could not write geenral purpose registers");
}

// implementing creating a break point site in memory
kdebugger::breakpoint_site & kdebugger::process::create_breakpoint_site(virt_addr address) {
	if(m_BreakPointSites.contains_address(address)) {
		error::send("Breakpoint at address has already been created" 
				+ std::to_string(address.addr()));
	}

	return m_BreakPointSites.push(std::unique_ptr<breakpoint_site> (new breakpoint_site (*this, address)));
}

// for stepping over instructions using single step
kdebugger::stop_reason kdebugger::process::step_instruction() {
	std::optional<breakpoint_site *> to_reenable;
	auto pc = get_pc();

	if(m_BreakPointSites.enable_stoppoint_at_address(pc)) {
		auto & bp = m_BreakPointSites.get_by_address(pc);
		bp.disable();
		to_reenable = &bp;
	}

	if(ptrace(PTRACE_SINGLESTEP, m_Pid, nullptr, nullptr) < 0)
		error::send_errno("Could not single step");

	auto reason = wait_on_signal();
	if(to_reenable)
		to_reenable.value()->enable();

	return reason;
}

std::vector<std::byte> kdebugger::process::read_memory(virt_addr address, std::size_t amount) const {
	std::vector<std::byte> ret{amount};
	
	iovec local_desc {ret.data(), ret.size()};
	std::vector<iovec> remote_descs;
	while(amount > 0) {
		auto up_to_next_page = 0x1000 - (address.addr() & 0xfff);
		auto chunk_size = std::min(amount, up_to_next_page);
		remote_descs.push_back({reinterpret_cast<void *>(address.addr()), chunk_size});
		
		amount -= chunk_size;
		address += chunk_size;
	}

	if(process_vm_readv(m_Pid, &local_desc, 1, remote_descs.data(), remote_descs.size(), 0) < 0)
		error::send_errno("Could not read process memory!\n");

	return ret;
}

std::vector<std::byte> kdebugger::process::read_memory_without_traps(virt_addr address, std::size_t amount ) const {
	auto memory = read_memory(address, amount);
	auto sites = m_BreakPointSites.get_in_region(address, address + amount);

	for(auto site : sites) {
		if(!site->is_enabled())
			continue;

		auto offset = site->address() - address.addr();
		memory[offset.addr()] = site->m_SavedData;
	}

	return memory;
}

void kdebugger::process::write_memory(virt_addr address, span<const std::byte> data) {
	std::size_t written {0};

	while(written < data.size()) {
		auto remaining = data.size() - written;
		std::uint64_t word;

		if(remaining >= 8)
			word = from_bytes<std::uint64_t> (data.begin() + written);
		else {
			auto read = read_memory(address + written, 8);
			auto word_data = reinterpret_cast<char *> (&word);
			
			std::memcpy(word_data, data.begin() + written, remaining);
			std::memcpy(word_data + remaining, read.data() + remaining, 8 - remaining);
		}

		if(ptrace(PTRACE_POKEDATA, m_Pid, address + written, word) < 0)
			error::send_errno("Failed to write memory");

		written += 8;
	}
}	
