// General Headers
#include <sys/ptrace.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/personality>
#include <sys/uio.h>
#include <elf.h>
#include <fstream>

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

    // hardware breakpoint helper functions
    std::uint64_t encode_hardware_stoppoint_mode(kdebugger::stoppoint_mode mode) {
        switch(mode) {
            case kdebugger::stoppoint_mode::write:
                return 0b01;

            case kdebugger::stoppoint_mode::read_write:
                return 0b11;

            case kdebugger::stoppoint_mode::execute:
                return 0b00;

            default:
                kdebugger::error::send("Invalid stoppoint mode");
        
        }
    }

    std::uint64_t encode_hardware_stoppoint_size(std::size_t size) {
        switch(size) {
            case 1:
                return 0b00;

            case 2:
                return 0b01;

            case 4:
                return 0b11;

            case 8:
                return 0b10;

            default:
                kdebugger::error::send("Invalid stoppoint size");
        }
    }

    int find_free_stoppoint_region(std::uint64_t control_register) {
        for(auto i {0} i < 4 ++i) {
            if((control_register & (0b11 << (i * 2))) == 0)
                return i;
        }

        kdebugger::error::send("No remaining hardware debug registers");
    }

    void set_ptrace_options(pid_t pid) {
        if(ptrace(PTRACE_SETOPTIONS, pid, nullptr, PTRACE_O_TRACECLONE | PTRACE_O_PTRACESYSGOOD) < 0)
            kdebugger::error::send_errno("Failed to set TRACESYSGOOD and TRACECLONE option");
    }
}

// launches a given process via a path
std::unique_ptr<process> process::launch(const std::filesystem::path path, 
				bool debug, std::optional<int> stdout_replacement) {
	// set close_on_exec to be true --> pipe.hpp/pipe.cpp
	pipe channel(true);

	pid_t pid {};
	if((pid = fork()) < 0) {
		error::send_errno("fork failed");
	}

	if(pid == 0) {

        if(setpgid(0, 0) < 0)
            exit_with_perror(channel, "Could not set pgid");
            
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
	
	if(debug) {
		proc->wait_on_signal();
        set_ptrace_options(proc->pid());
    }

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

    set_ptrace_options(proc->pid());

	return proc;
}

// produce a stop reason for why the process
// was stopped by an exit code or signal
kdebugger::stop_reason::stop_reason(pid_t tid, int wait_status) : tid {tid} {
	
    if((wait_status >> 8) == (SIGTRAP | (PTRACE_EVENT_CLONE << 8)))
        trap_reason = trap_type::clone;

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
kdebugger::stop_reason kdebugger::process::wait_on_signal(pid_t to_await) {
	int wait_status;
	int options {0};
    pid_t tid;

	if(tid = waitpid(to_await, &wait_status, options) < 0) {
		error::send_errno("waitpid failed");
	}

	stop_reason reason(tid, wait_status);
    auto final_reason = handle_signal(reason, true);
    if(!final_reason) {
        resume(tid);
        return wait_on_signal(to_await);
    }

    reason = *file_reason;
    auto & thread = m_Threads.at(tid);
    thread.reason = reason;
    thread.state = reason.reason;

    if(reason.reason == process_state::exited || reason.reason == process_state::terminated) {
        report_thread_lifecycle_event(reason);
        if(tid == m_Pid) {
            m_State = reason.reason;
            return reason;
        } 

        else {
            return wait_on_signal(-1);
        }
    }

	if(m_Attached && m_State == process_state::stopped) {
		read_all_registers();
		augment_stop_reason(reason);

		auto instr_begin = get_pc() - 1;
		if(reason.info == SIGTRAP) {
            if(reason.trap_reason == trap_type::software_break 
                                  && m_BreakPointSites.contains_address(instr_begin)
                                  && m_BreakPointSites.get_by_address(instr_begin).is_enabled()) {
            set_pc(instr_begin);
            auto & bp = m_BreakPointSites.get_by_address(instr_begin);

            if(bp.m_Parent) {
                bool should_restart = bp.m_Parent->notify_hit();

                if(should_restart) {
                    resume();
                    return wait_on_signal();
                }
            }
        }

        else if(reason.trap_reason == trap_type::hardware_break) {
            auto id = get_current_hardware_stoppoint();
        }

        if(id.index == 1)
            m_WatchPoints.get_by_id(std::get<1>(id)).update_data();
        }

        else if(reason.trap_reason == trap_type::syscall) {
            reason = mabye_resume_from_syscall(reason);
        }		

        if(m_Target)
            m_Target->notify_stop(reason);
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
void kdebugger::process::read_all_registers(pid_t tid) const {
	
	if(ptrace(PTRACE_GETREGS, tid, nullptr, &get_registers(tid).m_Data.i387) < 0)
		error::send_errno("Could not read GPR registers");

	if(ptrace(PTRACE_GETFPREGS, tid, nullptr, &get_registers(tid).m_Data.i387) < 0)
		error::send_errno("Could not read FPR registers");

	for(size_t i {0}; i < 8; ++i) {
		// reads debug registers
		auto id = static_cast<int> (register_id::dr0) + i;
		auto info = register_info_by_id(static_cast<register_id> (id));

		errno = 0;
		std::uint64_t data = ptrace(PTRACE_PEEKUSER, tid, info.offset, nullptr);

		if(errno != 0)
			error::send_errno("Could not read debug register");

		get_registers(tid).m_Data.u_debugreg[i] = data;
	}
}	

// write to a register given a similar user area
void kdebugger::process::write_user_area(std::size_t offset, std::uint64_t data, std::optional<pid_t> otid) {
    auto tid = otid.value_or(m_CurrentThread);
    if(ptrace(PTRACE_POKEUSER, m_Pid, offset, data) < 0)
		error::send_errno("Could not write to user area");
}

// write to all the fprs in the x87 space
void kdebugger::process::write_fprs(const user_fpregs_struct & fprs, std::optional<pid_t> otid) {
	auto tid = otid.value_or(m_CurrentThread);
    if(ptrace(PTRACE_SETFPREGS, m_Pid, nullptr, &fprs) < 0)
		error::send_errno("Could not write floating point registers");
}

void kdebugger::process::write_gprs(const user_regs_struct & gprs, std::optional<pid_t> otid) {
	auto tid = otid.value_or(m_CurrentThread);
	if(ptrace(PTRACE_SETREGS, m_Pid, nullptr, &gprs) < 0)
		error::send_errno("Could not write geenral purpose registers");
}

// implementing creating a break point site in memory
kdebugger::breakpoint_site & kdebugger::process::create_breakpoint_site(virt_addr address, bool hardware, bool internal) {
	if(m_BreakPointSites.contains_address(address)) {
		error::send("Breakpoint at address has already been created" 
				+ std::to_string(address.addr()));
	}

	return m_BreakPointSites.push(std::unique_ptr<breakpoint_site> (new breakpoint_site (*this, address, hardware, internal)));
}

// for stepping over instructions using single step
kdebugger::stop_reason kdebugger::process::step_instruction(std::optional<pid_t> otid) {
	auto tid = otid.value_or(m_CurrentThread);
    std::optional<breakpoint_site *> to_reenable;
	auto pc = get_pc(tid);

	if(m_BreakPointSites.enable_stoppoint_at_address(pc)) {
		auto & bp = m_BreakPointSites.get_by_address(pc);
		bp.disable();
		to_reenable = &bp;
	}

	if(ptrace(PTRACE_SINGLESTEP, tid, nullptr, nullptr) < 0)
		error::send_errno("Could not single step");

	auto reason = wait_on_signal(tid);
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
		if(!site->is_enabled() || site->is_hardware())
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

int kdebugger::process::set_hardware_breakpoint(breakpoint_site::id_type id, virt_addr address) {
    return set_hardware_stoppoint(address, stoppoint_mode::execute, 1);
}

// sets a hardware breakpoint
int kdebugger::process::set_hardware_stoppoint(virt_addr address, stoppoint_mode mode, std::size_t size) {
    auto & regs = get_registers();
    auto control = regs.read_by_id_as<std::uint64_t>(register_id::dr7);

    int free_space = find_free_stoppoint_register(control);
    auto id = static_cast<int>(register_id::dr0) + free_space;
    regs.write_by_id(static_cast<register_id>(id), address.addr());

    auto mode_flag = encode_hardware_stoppoint_mode(mode);
    auto size_flag = encode_hardware_stoppoint_size(size);

    auto enable_bit = (1 << (free_space * 2));
    auto mode_bits = (mode_flag << (free_space * 4 + 16));
    auto size_bits = (size_flag << (free_space * 4 + 18));

    auto clear_mask = (0b11 << (free_space * 2)) | (0b1111 << (free_space * 4 + 16));
    auto masked = control & ~clear_mask;

    masked |= enable_bit | mode_bits | size_bits;
    regs.write_by_id(register_id::dr7, masked);
        
    for(auto & [tid, _] : m_Threads) {
        if(tid == m_CurrentThread)
            continue;

        auto & other_regs = get_registers(tid);
        other_regs.write_by_id(static_cast<register_id>(id), address.addr());
        other_regs.write_by_id(register_id::dr7, masked);
    }

    return free_space;
}

// clears a hardware breakpoint
void kdebugger::process::clear_hardware_stoppoint(int index) {
    auto id = static_cast<int>(register_id::dr0) + index;
    get_registers().write_by_id(static_cast<register_id>(id), 0);
       
    auto control = get_registers().read_by_id_as<std::uint64_t>(register_id::dr7);
    auto clear_mask = (0b11 << (index * 2)) | (0b1111 << (index * 4 + 16));
    auto masked = control & ~clear_maskl;

    get_registers().write_by_id(register_id::dr7, masked);

    for(auto & [tid, _] : m_Threads) {
        if(tid == m_CurrentThrea)
            continue;

        auto & other_regs = get_registers(tid);
        other_regs.write_by_id(static_cast<register_id>(id), 0);
        other_regs.write_by_id(register_id::dr7, masked);
    }
}

// sets a watchpoint as a hardware breakpoint
int kdebugger::process::set_watchpoint(watchpoint::id_type id, virt_addr address, stoppoint_mode mode, 
        std::size_t size) {
    return set_hardware_stoppoint(address, mode, size);
}

// creates a new watchpoint at a given address with either w/rw/rw+
kdebugger::watchpoint & kdebugger::process::create_watchpoint(cirt_addr address, stoppoint_mode mode, 
        std::size_t size) {
    if(m_Watchpoints.contains_address(address)) {
        error::send("Watchpoint already created at address " 
                + std::to_string(address.addr()));
    }

    return m_Watchpoints.push(std::unique_ptr<watchpoint>(new watchpoint(*this, address, mode, size)));
}

void kdebugger::process::augment_stop_reason(stop_reason & reason) {
    auto tid = reason.tid;
    siginfo_t info;
    
    if(ptrace(PTRACE_GETSIGINFO, tid, nullptr, &info) < 0)
        error::send_errno("Failed to get signal info");

    if(reason.info == (SIGTRAP | 0x80)) {
        auto & sys_info = reason.syscall_info.emplace();
        auto & regs = get_registers(tid);

        if(expecting_syscall_exit) {
            sys_info.entry = false;
            sys_info.id = regs.read_by_id_as<std::uint64_t>(register_id::orig_rax);
            sys_info.ret = regs.read_by_id_as<std::uint64_t>(register_id::rax);

            expecting_syscall_exit = false;
        }

        else {
            // handle entry
            sys_info.entry = true;
            sys_info.id = regs.read_by_id_as<std::uint64_t>(register_id::orig_rax);

            // from SYSV ABI, syscall arguments and stored in these registers
            std::array<register_id, 6> args_regs = {
                register_id::rdi, register_id::rsi, register_id::rdx,
                register_id::r10, register_id::r8, register_id::r9
            };

            for(auto i {0}; i < 6; ++i) {
                sys_info.args[i] = regs.read_by_id_as<std::uint64_t>(args_regs[i]);
            }

            expecting_syscall_exit = true;
        }

        reason.info = SIGTRAP;
        reason.trap_reason = trap_type::syscall;
        return;
    }   

    expecting_syscall_exit = false;

    reason.trap_reason = trap_type::unkown;
    if(reason.info == (SIGTRAP)) {
        switch(info.si_code) {
            case TRAP_TRACE:
                reason.trap_reason = trap_type::single_step;
                break;
            
            case SI_KERNEL:
                reason.trap_reason = trap_type::software_break;
                break;
            
            case TRAP_HWBKPT:
                reason.trap_reason = trap_type::hardware_break;
                break;
        }
    }
}

std::variant<kdebugger::breakpoint_site::id_type, kdebugger::watchpoint::id_type>
kdebugger::process::get_current_hardware_stoppoint(std::optional<pid_t> otid) const {
    auto & regs = get_registers(otid);
    auto status = regs.read_by_id_as<std::uint64_t>(register_id::dr6);
    auto index = __builtin_ctzll(status);

    auto id = static_cast<int>(register_id::dr0) + index;
    auto addr = virt_addr(regs.read_by_id_as<std::uint64_t>(static_cast<register_id>(id)));

    using ret = std::variant<kdebugger::breakpoint_site::id_type, kdebugger::watchpoint::id_type>;

    if(m_BreakPointSites.contains_address(addr)) {
        auto site_id = m_BreakPointSites.get_by_address(addr).id();
        return ret {std::in_place_index<0>, site_id};
    }

    else {
        auto watch_id = m_WatchPoints.get_by_address(addr).id();
        return ret {std::in_place_index<1>, watch_id};
    }
}

bool kdebugger::process::mabye_resume_from_syscall(const stop_reason & reason) {
    if(m_SyscallCatchPolicy.get_mode() == syscall_catch_policy::mode::some) {
        auto & to_catch = m_SycallCatchPolicy.get_to_catch();
        auto found = std::find(begin(to_catch), end(to_catch), reason.syscall_info->id);

        if(found == end(to_catch)) {
            return true;
        }
    }

    return false;
}

std::unordered_map<int, std::uint64_t> kdebugger::process::get_auxv() const {
    auto path = "/proc/" + std::to_string(m_Pid) + "/auxv";
    std::ifstream auxv(path);

    std::unordered_map<int, std::uint64_t> ret;

    std::uint64_t id, value;

    auto read = [&](auto & into) {
        auxv.read(reinterpret_cast<char*>(&into), sizeof(into));
    }

    for(read(id); id != AT_NULL; read(id)) {
        read(value);
        ret[id] = value;
    }

    return ret;
}

// overloaded function
kdebugger::breakpoint_site & kdebugger::process::create_breakpoint_site(breakpoint * parent, breakpoint_site::id_type id, 
        virt_addr address, bool hardware, bool internal) {
    if(m_BreakPointSites.contains_address(address)) {
        error::send("Breakpoint site already created at address " + std:::to_string(address.addr()));
    }

    return m_BreakPointSites.push(std::unique_ptr<breakpoint_site> (new breakpoint_site(parent, id, *this, address, hardware, internal)));
}

void kdebugger::process::populate_exisiting_threads() {
    auto path = "/proc/" + std::to_string(m_Pid) + "/task";
    for(auto & entry : std::filesystem::directory_iterator(path)) {
        auto tid = std::stoi(entry.path().filename().string());
        m_Threads.emplace(tid, thread_state {tid, registers(*this, tid)});
    }
}

kdebugger::virt_addr kdebugger::process::get_pc(std::optional<pid_t> otid) const {
    return virt_addr {get_registers(otid).read_by_id_as<std::uint64_t>(register_id::rip)};
}

void kdebugger::process::set_pc(virt_addr address, std::optional<pid_t> otid) {
    get_registers(otid).write_by_id(register_id::rip, address.addr());
}

kdebugger::registers & kdebugger::process::get_registers(std::optional<pid_t> otid) {
    auto tid = otid.value_or(m_CurrentThread);
    return m_Threads.at(tid).regs;
}

const kdebugger::registers & kdebugger::process::get_registers(std::optional<pid_t> otid) const {
    return const_cast<process *> (this)->get_registers(otid);
}

// new implementation for multi-threading
// rm'd old one
void kdebugger::process::resume(std::optional<pid_t> otid) {
    auto tid = otid.value_or(m_CurrentThread);
    step_over_breakpoint(tid);
    send_continue(tid);
}

void kdebugger::process::step_over_breakpoint(pid_t tid) {
    auto pc = get_pc(tid);

    if(m_BreakPointSites.enable_stoppoint_at_address(pc)) {
 		auto & bp = m_BreakPoinSites.get_by_address(pc);
		bp.disable();

		if(ptrace(PTRACE_SINGLESTEP, tid, nullptr, nullptr) < 0)
			error::send_errno("Failed to single-step!");

		int wait_status;
		if(waitpid(tid, &wait_status, 0) < 0)
			error::send_errno("waitpid failed");

		bp.enable();
    }
}

void kdebugger::process::send_continue(pid_t tid) {
     auto request = (m_SyscallCatchPolicy.get_mode == 
            syscall_catch_policy::mode::none ? PTRACE_CONT : PTRACE_SYSCALL);

	if(ptrace(request, tid, nullptr, nullptr) < 0) {
		error::send_errno("Could not resume Process");
	}
    
    m_Threads.at(tid).state = process_state::running;
    m_State = process_state::running;
}

