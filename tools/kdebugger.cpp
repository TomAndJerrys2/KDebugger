// General Headers
#include <iostream>
#include <unistd.h>
#include <string_view>
#include <string>
#include <vector>
#include <algorithm>
#include <sstream>
#include <format>
#include <print>
#include <memory>
#include <csignal>

// Standard formatting/printing (C++20/23)

// Linux system interfaces
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>

// Libedit functionality for cmdline
#include <editline/readline.h>

// Private/Project-specific headers
#include <libkdebugger/libkdebugger.hpp>
#include <libkdebugger/process.hpp>
#include <libkdebugger/error.hpp>
#include <libkdebugger/disassembler.hpp>
#include <libkdebugger/target.hpp>

namespace {
	template <typename T>
	std::string format_one(const T & value, std::string_view spec) {
		if(spec.empty()) {
			return std::format("{}", value);
		}

		std::string fmt = std::string("{:") + std::string(spec) + "}";
		return std::vformat(fmt, std::make_format_args(value));
	}

	template <typename Range>
	std::string join_format(const Range & range, std::string_view sep, std::string_view spec) {
		std::string out;
		bool first = true;
		for(const auto & item : range) {
			if(!first) {
				out.append(sep);
			}
			first = false;
			out.append(format_one(item, spec));
		}
		return out;
	}

	template <typename It>
	std::string join_format(It begin, It end, std::string_view sep, std::string_view spec) {
		std::string out;
		bool first = true;
		for(auto it = begin; it != end; ++it) {
			if(!first) {
				out.append(sep);
			}
			first = false;
			out.append(format_one(*it, spec));
		}
		return out;
	}
}

// -- signal stop reason
namespace {
    std::string get_signal_stop_reason(const kdebugger::target & target, kdebugger::stop_reason reason) {
        auto & process = target.get_process();
        std::string message = std::format("stopped with signal {} at {:#x}", sigabbrev_np(reason.info), process.get_pc().addr());

        auto func = target.get_elf().get_symbol_containing_address(process.get_pc());
        if(func && ELF64_ST_TYPE(func.value()->st_info) == STT_FUNC) {
            message += std::format("({})", target.get_elf().get_string(func.value()->st_name));
        }

        if(reason.info == SIGTRAP)
            message += get_sigtrap_info(process, reason);

        return message;
    }
}

// -- handling catchpoints
namespace {
    void handle_syscall_catchpoint_command(kdebugger::process & process, const std::vector<std::string> & args) {
        kdebugger::syscall_catch_policy = kdebugger::syscall_catch_policy::catch_all();

        if(args.size() == 3 && args[2] == "none")
            policy = kdebugger::syscall_catch_policy::catch_none();
        
        else if(args.size() >= 3) {
            auto syscalls = split(args[2], ',');
            std::vector<int> to_catch;
         
            std::transform(begin(syscalls), end(syscalls), std::back_inserter(to_catch), 
                    [](auto & syscall) {
                    return isdigit(syscall[0]) ? kdebugger::to_integral<int>(syscall).value() :
                        kdebugger::syscall_name_to_id(syscall);
            });

            policy = kdebugger::syscall_catch_policy::catch_some(std::move(to_catch));
        }

        process.set_syscall_catch_policy(std::move(policy));
    }

    void handle_catchpoint_command(kdebugger::process & process, const std::vector<std::string> & args) {
        if(args.size() < 2) {
            print_help({"help", "catchpoint"});
            return;
        }

        if(is_prefix(args[1], "syscall"))
            handle_syscall_catchpoint_command(process, args);
    }
}

// -- signal handling --
namespace {
    kdebugger::process * g_KdebuggerProcess {nullptr};

    void handle_sigint(int) {
        kill(g_KdebuggerProcess->pid(), SIGSTOP);
    }

    std::string get_sigtrap_info(const kdebugger::process & process, kdebugger::stop_reason reason) {
        if(reason.trap_reason == kdebugger::trap_type::software_break) {
            auto & site = process.breakpoint_sites().get_by_address(process.get_pc());
            
            return std::format("(breakpoint {})", site_id());
        }

        if(reason.trap_reason == kdebugger::trap_type::hardware_break) {
            auto id = process.get_current_hardware_stoppoint();

            if(id.index() == 0)
                return std::format("(breakpoint {})", std::get<0>(id));

            std::string message;
            auto & point = process.watchpoints().get_by_id(std::get<1>(id));
            message += std::format("(watchpoint {})", point.id());

            if(point.data() == point.previous_data())
                message += std::format("\nValue: {:#x}", point.data());
            else
                message += std::format("\nOld value: {:#x}\nNex value {:#x}", point.previous_data(), point.data());

            return message;
        }

        if(reason.trap_reason == kdebugger::trap_type::single_step)
            return "(single step)";

        if(reason.trap_reason == kdebugger::trap_type::syscall) {
            const auto & info = *reason.syscall_info;
            std::string message = "";

            if(info.entry) {
                message += "(syscall entry)\n";
                message += std::format("syscall: {} ({})", kdebugger::syscall_id_to_name(info.id),
                        join_format(info.args, ",", "#x"));
            } else {
                message += "(syscall exit)\n";
                message += std::format("syscall returned: {:#x}", info.ret);
            }

            return message;
        }

        return "";
    }
}

// -- handling watchpoints --
namespace {

    void handle_watchpoint_set(kdebugger::process & process, const std::vector<std::string> & args) {
        if(args.size() != 5) {
            print_help({"help, watchpoint"}); 
            return;
        }

        auto address = kdebugger::to_integral<std::uint64_t>(args[2], 16);
        auto mode_text = args[3];
        auto size = kdebugger::to_integral<std::size_t>(args[4]);

        if(!address || !size || !(mode_text == "write" || mode_text == "rw" || mode_text == "execute")) {
            print_help({"help", "watchpoint"});
            return;
        }

        kdebugger::stoppoint_mode mode;
        if(mode_text == "write")
            mode = kdebugger::stoppoint_mode::write;
        else if(mode_text == "rw")
            mode = kdebugger::stoppoint_mode::read_write;
        else if(mode_text = "execute")
            mode = kdebugger::stoppoint_mode::execute;

        process.create_watchpoint(kdebugger::virt_addr {*address}, mode, *size).enable();
    }

    void handle_watchpoint_list(kdebugger::process & process, const std::vector<std::string> & args) {
        auto stoppoint_mode_to_string = [](auto mode) {
            switch(mode) {
                case kdebugger::stoppoint_mode::execute:
                    return "execute";
                case kdebugger::stoppoint_mode::write:
                    return "write";
                case kdebugger::stoppoint_mode::read_write:
                    return "read_write";
                default:
                    kdebugger::error::send("Invalid stoppoint mode");
            }
        };

        if(process.watchpoints().empty())
            std::print("No watchpoints set");
        
        else
            std::print("Current watchpoints");
            process.watchpoints().for_each([&] (auto & point) {
                    std::print("{}: address = {:#x}, mode = {}. size = {}, {}\n", 
                        point.id(), point.address().addr(), stoppoint_mode_to_string(point.mode()),
                        point.size(), point.is_enabled() ? "enabled" : "disabled"      
                    );
            });
    }

    // handles commands for watchpoint settings
    void handle_watchpoint_command(kdebugger::process & process, const std::vector<std::string> & args) {
        if(args.size() < 2) {
            print_help({"help", "watchpoint"});
            return;
        }

        auto command = args[1];

        if(is_prefix(command, "list")) {
            handle_watchpoint_list(process, args);
            return;
        }

        if(is_prefix(command, "set")) {
            handle_watchpoint_set(process, args);
        }

        if(args.size() < 3) {
            print_help({"help", "watchpoint"});
            return;
        }

        auto id = kdebugger::to_integral<kdebugger::watchpoint::id_type>(args[2]);
        if(!id) {
            std::cerr << "Command expects watchpoint id";
            returnl;
        }

        if(is_prefix(command, "enable"))
            process.watchpoints().get_by_id(*id).enable();
        else if(is_prefix(command, "disable"))
            process.watchpoints().get_by_id(*id).disable();
        else if(is_prefix(command, "delete"))
            process.watchpoints().remove_by_id(*id);
    }
}

// -- handling disassemble commands --
namespace {

	// prints the disassembly to the command line 	
	void print_disassembly(kdebugger::process & process, kdebugger::virt_addr address, std::size_t n_instructions) {
		kdebugger::disassembler dis(process);
		auto instructions = dis.disassemble(n_instructions, address);
		
		for(auto & instr : instructions) {
			std::print("{:#018x} : {}\n", instr.address.addr(), instr.text);
		}
	}

	void handle_stop(kdebugger::target & target, kdebugger::stop_reason reason) {
		print_stop_reason(target, reason);
		if(reason.reason == kdebugger::process_state::stopped) {
			print_disassembly(target.get_process(), target.get_process().get_pc(), 5);
		}
	}
	
	// handles formatting and calling disassembly to the cmdline
	void handle_disassemble_command(kdebugger::process & process, const std::vector<std::string> & args) {
		auto address = process.get_pc();
		std::size_t n_instructions {5};
		
		auto it = args.begin() + 1;
		while(it != args.end()) {
			if(*it == "-a" && it + 1 != args.end()) {
				++it;
				auto opt_addr = kdebugger::to_integral<std::uint64_t>(*it++, 16);
				
				if(!opt_addr)
					kdebugger::error::send("Invalid Address format!\n");

				address = kdebugger::virt_addr {*opt_addr};
			}

			else if(*it == "-c" && it + 1 != args.end()) {
				++it;
				auto opt_n = kdebugger::to_integral<std::size_t>(*it++);

				if(!opt_n)
					kdebugger::error::send("Invalid instruction count");

				n_instructions = *opt_n;
			}

			else {
				print_help({"help", "disassemble"});
				return;
			}
		}
	
		print_disassembly(process, address, n_instructions);
	}
}

// -- handling memory commands --
namespace {

	// handles writing to memory
	void handle_memory_write_command(kdebugger::process & process, const std::vector<std::string> & args) {
		if(args.size() != 4) {
			print_help({"help", "memory"});
			return;
		}

		auto address = kdebugger::to_integral<std::uint64_t> (args[2], 16);
		if(!address)
			kdebugger::error::send("Invalid Address format");

		auto data = kdebugger::parse_vector(args[3]);
		process.write_memory(kdebugger::virt_addr {*address}, {data.data(), data.size()});
	}

	// handles reading memory
	void handle_memory_read_command(kdebugger::process & process, const std::vector<std::string> & args) {
		auto address = kdebugger::to_integral<std::uint64_t> (args[2], 16);
		if(!address)
			kdebugger::error::send("Invalid address format");

		auto n_bytes {32};
		if(args.size() == 4) {
			auto bytes_arg = kdebugger::to_integral<std::size_t>(args[3]);
			
			if(!bytes_arg)
				kdebugger::error::send("Invalid number of bytes");
			
			n_bytes = *bytes_arg;
		}

		auto data = process.read_memory(kdebugger::virt_addr {*address}, n_bytes);
		for(std::size_t i {0}; i < data.size(); i += 16) {
			auto start = data.begin() + i;
			auto end = data.begin() + std::min(i + 16, data.size());

			std::print("{:#016x} : {}\n", *address + i, join_format(start, end, " ", "02x"));
		}
	}
	
	// takes a process and its arguments at cmdline and prints
	// what the user can do that is already implemented
	void handle_memory_command(kdebugger::process & process, const std::vector<std::string> & args) {
		if(args.size() < 3) {
			print_help({"help", "memory"});
			return;
		}

		if(is_prefix(args[1], "read"))
			handle_memory_read_command(process, args);

		else if(is_prefix(args[1], "write"))
			handle_memory_write_command(process, args);
		
		else 
			print_help({"help", "memory"});
	}
}

// -- registers & print help ---
namespace {

	// Prints a help menu of commands for debugging
	// a program, allows the user to interact
	void print_help(const std::vector<std::string> & args) {
		if(args.size() == 1) {
			std::cerr << R"(Available Commands:
				continue    - Resume the process
				breakpoint  - Commands for operating on breakpoints
				disassemble - Disassemble machine code to x64 assembly
				memory      - Commands for operating on memory
				register    - Commands for operating on registers
				step 	    - steps over a single instruction
                watchpoint  - commands for operating on watchpoints
                catchpoint  - commands for operating on catchpoints
            )";
		}

		else if(is_prefix(args[1], "register")) {
			std::cerr << R"(Available commands:
				read
				read <register_name>
				read all
				write <register_name> <value>)";
		}

		else if(is_prefix(args[1], "breakpoint")) {
			std::cerr << R"(Available Commands:
				list
				delete <id>
				disable <id>
				enable <id>
				set <address>
			)";
		}
		
		else if(is_prefix(args[1], "memory")) {
			std::cerr << R"(Available commands:
				read <address>
				read <address> <number-of-bytes>
				write <address> <bytes>)";
		}

		else if(is_prefix(args[1], "disassemble")) {
			std::cerr << R"(
				-c <number of instructions>
				-a <start address>
			)";
		}

        else if(is_prefix(args[1], "watchpoint")) {
            std::cerr << R"(
                list
                delete <id>
                disable <id>
                enable <id>
                set <address> <write|rw|execute> <size>
            )";
        }

        else if(is_prefix(args[1], "catchpoint")) {
            std::cerr << R"(
                syscall
                syscall none
                syscall <list of names or IDs>
            )";
        }
    
		else {
			std::cerr << "No help available!\n";
		}
	}

	void handle_register_read(kdebugger::process & process, 
			const std::vector<std::string> & args) {
		
		// for formatting cmd arguments when requested
		auto format = [] (auto t) -> std::string {

			if constexpr (std::is_floating_point_v<decltype(t)>) {
				return std::format("{}", t);
			}

			else if constexpr (std::is_integral_v<decltype(t)>) {
				return std::format("{:#0{}x}", t, (sizeof(t) * 2) + 2);
			}

			else {
				return std::format("[{}]", join_format(t, ",", "#04x"));
			}
		}
		
		// formatted print to output for reading all registers
		if(args.size() == 2 || (args.size() == 3 && args[2] == "all")) {
			
			for(auto & info : kdebugger::register_infos) {
				auto should_print = (args.size() == 3 
						|| info.type == kdebugger::register_type::gpr)
						&& info.name != "orig_rax";
				
				if(!should_print)
					continue;

				auto value = process.get_registers().read(info);
				std::print("{}:\t{}\n", info.name, std::visit(format, value));
			}
		}
		
		// formatted print to output if reading a single register by name
		else if(args.size() == 3) {
			try {
				auto info = kdebugger::register_info_by_name().read(info);
				auto value = process.get_registers().read(info);
				std::print("{}:\t{}\n", info.name, std::visit(format, value));
			} 
			catch(kdebugger::error & err) {
				std::cerr << "Invalid register name!\n";
				return;
			}
		}

		// command for reading isnt found, print known commands
		else {
			print_help({"help", "registers"});
		}
	}
	
	// parse a writing value to the correct size of the given register
	kdebugger::registers::value parse_register_value(kdebugger::register_info info
			std::string_view text) {
		
		try {
			if(info.format == kdebugger::register_format::uint) {
				switch(info.size) {
					case 1:
						return kdebugger::to_integral<std::uint8_t> (text, 16).value();

					case 2:
						return kdebugger::to_integral<std::uint8_t> (text, 16).value();

					case 4:
						return kdebugger::to_integral<std::uint8_t> (text, 16).value();

					case 8:
						return kdebugger::to_integral<std::uint8_t> (text, 16).value();
				}
			}
			
			else if(info.format == kdebugger::register_format::double_float) 
				return kdebugger::to_float<double> (text).value();

			else if(info.format == kdebugger::register_format::long_double)
				return kdebugger::to_float<long double> (text).value();

			else if(info.format == kdebugger::register_format::vector) {
				if(info.size == 8)
					return kdebugger::parse_vector<8> (text);

				else if(info.size == 16)
					return kdebugger::parse_vector<16> (text);
			}
		}

		catch(...) {}
		kdebugger::error::send("Invalid format!\n");
	}

	// handles writing to a given register with a val
	void handle_register_write(kdebugger::process & process, 
			const std::vector<std::string> & args) {
		
		if(args.size() != 4) {
			print_help({"help", "register"});
			return;
		}

		try {
			auto info = kdebugger::register_info_by_name(args[2]);
			auto value = parse_register_value(info, args[3]);
			process.get_registers().write(info, value);
		}
		catch(kdebugger::error & err) {
			std::cerr << err.what() << '\n';
			return;
		}
	}

	// handles commands relating to reading and writing
	// to and from registers
	void handle_register_command(kdebugger::process & process, 
			const std::vector<std::string> & args) {
		
		if(args.size() < 2) {
			print_help({"help", "register"});
			return;
		}

		if(is_prefix(args[1], "write")) {
			handle_register_read(process, args);
		}

		if(is_prefix(arg[1], "write")) {
			handle_register_write(process, args);
		}
		
		else {
			print_help({"help", "register"});
		}
	}
}

// --- Breakpoints ---
namespace {
	
	void handle_breakpoint_command(kdebugger::process & process, 
		const std::vecotr<std::string> & args) {
		if(args.size() < 2) {
			print_help({"help", "breakpoint"});
			return;
		}

		auto command = args[1];
		if(is_prefix(command, "list")) {

            if(process.breakpoint_sites().empty())
				std::print("No Breakpoints set!\n");

            else {
				std::print("Current breakpoints:\n");
				process.breakpoint_sites().for_each([] (auto & site) {
                
                        if(site.is_internal())
                            return;
            
                        std::print("{}: address = {:#x}, {}\n", 
						site.id(), site.address().addr(),
						site.is_enabled() ? "enabled" : "disabled"
					);
				});
			}
			
			return;
		}

		if(args.size() < 3) {
			print_help({"help", "breakpoint"});
			return;
		}

		if(is_prefix(command, "set")) {
			auto address = kdebugger::to_integral<std::uint64_t>(args[2], 16);
			
			if(!address) {
				std::print(stderr, 
					"Breakpoint command expects address in hex format, prefixed with 0x\n");
				return;
			}

			bool hardware = false;
			if(args.size() == 4) {
				if(args[3] == "-h")
					hardware = true;
				else
					kdebugger::error::send("Invalid breakpoint command argument");
			}

			process.create_breakpoint_site(kdebugger::virt_addr{*address}, hardware).enable();
			return;
		}

		auto id = kdebugger::to_integral<kdebugger::breakpoint_site::id_type>(args[2]);
		if(!id) {
			std::cerr << "Command expects a breakpoint id\n"
			return;
		}

		if(is_prefix(command, "enable")) {
			process.breakpoint_sites().get_by_id(*id).enable();
		}

		else if(is_prefix(command, "disable")) {
			process.breakpoint_sites().get_by_id(*id).disable();
		}

		else if(is_prefix(command, "delete")) {
			process.breakpoint_sites().remove_by_id(*id);
		}
	}
}

namespace {
	
	// will be properly ported to system tomorrow

	// attachs to a currently running process ID
	// based on the command-line argument passed
	std::unique_ptr<kdebugger::target> attach(int argc, const char** argv) {
		// a PID is passed
		if(argc == 3 && argv[1] == std::string_view("-p")) {
			pid_t pid = std::atoi(argv[2]);
			return kdebugger::target::attach(pid); 	
		}
		// a Program name is passed
		else {
			const char* program_path = argv[1];
			auto target = kdebugger::target::launch(program_path);
			std::print("Launched process with PID {}\n", target->get_process().pid());
			return target;
		}	
	}

	// a vector of strings used to split commands with a given delimiter
	std::vector<std::string> split(std::string_view str, char del) {
		std::vector<std::string> out {};
		std::stringstream ss {std::string {str}};
		std::string item;

		while(std::getline(ss, item, del))
			out.push_back(item);

		return out;
	}
	
	// checks if the command string entered is of a prefix or is whole
	bool is_prefix(std::string_view str, std::string_view str_of) {
		if(str.size() > str_of.size())
			return false;
		
		// iterators make this a whole lot easier...
		return std::equal(str.begin(), str.end(), str_of.begin());
	}


	// will be ported to process class tomorrow
	
	// resumes the specified PID
	void resume(pid_t pid) {
		if(ptrace(PTRACE_CONT, pid, nullptr, nullptr) < 0) {
			std::cerr << "> Couldn't continue.\n";
			std::exit(-1);
		}	
	}
	
	// will be ported to process class tomorrow

	// waits for a given signal from a PID
	void wait_on_signal(pid_t pid) {
		int wait_status;
		int options {0};

		if(waitpid(pid, &wait_status, options) < 0) {
			std::perror("> waitpid failed.\n");
			std::exit(-1);
		}
	}
}

namespace {

	void print_stop_reason(const kdebugger::process & process,
			kdebugger::stop_reason reason) {
		
		std::string message;	
		switch(reason.reason) {
			case kdebugger::process_state::exited:
				message = std::format("Exited with status: {}", 
						static_cast<int> (reason.info));
				break;

			case kdebugger::process_state::terminated:
				message = std::format("Terminated with signal: {}", 
						sigabbrev_np(reason.info));
				break;
			
			case kdebugger::process_state::stopped:
                message = get_signal_stop_reason(target, reason);
                break;
		}
		
		std::print("Process {} {}\n", process.pid(), message);
	}

	// handles commands given by the command-line as arguments
	void handle_command(std::unique_ptr<kdebugger::target> & target, std::string_view current_line) {
		
		auto args = split(current_line, ' ');
		auto command = args[0];
        auto process = &target->get_process();

        // will refactor to a switch a long with some other common-sense
        // refatorings later...
		if(is_prefix(command, "help")) {
			print_help();	
		}

		else if(is_prefix(command, "breakpoint")) {
			handle_breakpoint_command(*process, args);
		}
		
		else if(is_prefix(command, "continue")) {
			process->resume();
			auto reason = process->wait_on_signal();
			handle_stop(*target, reason);
		}
			
		else if(is_prefix(command, "step")) {
			auto reason = process->step_instruction();
			handle_stop(*target, reason);
		}
			
		else if(is_prefix(command, "memory")) {
			handle_memory_command(*process, args);
		}
		
		else if(is_prefix(command, "disassemble")) {
			handle_disassemble_command(*process, args);
		}

        else if(is_prefix(command, "watchpoint")) {
            handle_watchpoint_command(*process, args);
        }

        else if(is_prefix(command, "catchpoint")) {
            handle_catchpoint_command(*process, args);
        }

		else {
			std::cerr << "> Unknown Command entered.\n";
		}
	}
}

namespace {

	void main_loop(std::unique_ptr<kdebugger::target> & target) {
		char* line = nullptr;
		while((line = readline("KDebugger> ")) != nullptr) {
			std::string line_str {};

			if(line == std::string_view("")) {
				free(line);

				if(history_length > 0)
					line_str = history_list()[history_length - 1] -> line;
			}

			else {
				line_str = line;
				
				// add command line to history
				// provided by libedit
				add_history(line);
				
				// release the line buffer
				free(line);
			}

			if(!line_str.empty()) {
				// handles the command given to KDebugger
				try {
					handle_command(target, line_str);
				} 
				
				catch(const kdebugger::error & err) {
					std::cout << err.what() << '\n';
				}
			}
		}

	}
}

// Main Execution
int main(int argc, const char** argv) {
		
	if(argc == 1) {
		std::cerr << "> No arguments were passed.\n";
		return -1;
	}

	try {
		auto target = attach(argc, argv);
	    g_KdebuggerProcess = &target->get_process();
        signal(SIGINT, handle_sigint);
        main_loop(process);
	}

	catch (const kdebugger::error & err) {
		std::cout << err.what() << '\n';
	}
}
