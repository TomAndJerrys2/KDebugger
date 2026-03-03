// General Headers
#include <iostream>
#include <unistd.h>
#include <string_view>
#include <string>
#include <vector>
#include <algorithm>
#include <sstream>
#include <memory>

// formatting options - will refactor to standard 20 usage
// after build completion
#include <fmt/format.h>
#include <fmt/ranges.h>

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

// -- handling disassemble commands --
namespace {

	// prints the disassembly to the command line 	
	void print_disassembly(kdebugger::process & process, kdebugger::virt_addr address, std::size_t n_instructions) {
		kdebugger::disassembler dis(process);
		auto instructions = dis.disassemble(n_instructions, address);
		
		for(auto & instr : instructions) {
			fmt::print("{:#018x} : {}\n", instr.address.addr(), instr.text);
		}
	}

	void handle_stop(kdebugger::process & process, kdebugger::stop_reason reason) {
		print_stop_reason(process, reason);
		if(reason.reason == kdebugger::process_state::stopped) {
			print_disassembly(process, process.get_pc(), 5);
		}
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

			fmt::print("{:#016x} : {:02x}\n", *address + i, fmt::join(start, end, " "));
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
				continue   - Resume the process
				breakpoint - Commands for operating on breakpoints
				memory     - Commands for operating on memory
				register   - Commands for operating on registers
				step 	   - steps over a single instruction)";
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

		else {
			std::cerr << "No help available!\n";
		}
	}

	void handle_register_read(kdebugger::process & process, 
			const std::vector<std::string> & args) {
		
		// for formatting cmd arguments when requested
		auto format = [] (auto t) -> std::string {

			if constexpr (std::is_floating_point_v<decltype(t)>) {
				return fmt::format("{}", t);
			}

			else if constexpr (std::is_integral_v<decltype(t)>) {
				return fmt::format("{:#0{}x}", t, (sizeof(t) * 2) + 2);
			}

			else {
				return fmt::format("[{:#04x}]", fmt::join(t, ","));
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
				fmt::print("{}:\t{}\n", info.name, std::visit(format, value));
			}
		}
		
		// formatted print to output if reading a single register by name
		else if(args.size() == 3) {
			try {
				auto info = kdebugger::register_info_by_name().read(info);
				auto value = process.get_registers().read(info);
				fmt::print("{}:\t{}\n", info.name, std::visit(format, value));
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

	// handles writing to a given register with a value
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
				fmt::print("No Breakpoints set!\n");
			else {
				fmt::print("Current breakpoints:\n");
				process.breakpoint_sites().for_each([] (auto & site) {
					fmt::print("{}: address = {:#x}, {}\n", 
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
				fmt::print(stderr, 
					"Breakpoint command expects address in hex format, prefixed with 0x\n");
				return;
			}

			process.create_breakpoint_site(kdebugger::virt_addr{*address}).enable();
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
	std::unique_ptr<kdebugger::process> attach(int argc, const char** argv) {
		// a PID is passed
		if(argc == 3 && argv[1] == std::string_view("-p")) {
			pid_t pid = std::atoi(argv[2]);
			return kdebugger::process::attach(pid); 	
		}
		// a Program name is passed
		else {
			const char* program_path = argv[1];
			auto proc = kdebugger::process::launch(program_path);
			fmt::print("Launched process with PID {}\n", proc->pid());
			return proc;
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
				message = fmt::format("Exited with status: {}", 
						static_cast<int> (reason.info));
				break;

			case kdebugger::process_state::terminated:
				message = fmt::format("Terminated with signal: {}", 
						sigabbrev_np(reason.info));
				break;
			
			case kdebugger::process_state::stopped:
				message = fmt::format("Stopped with signal:{}  at {:#x}", 
						sigabbrev_np(reason.info), process.get_pc().addr());
				break;
		}
		
		fmt::print("Process {} {}\n", process.pid(), message);
	}

	// handles commands given by the command-line as arguments
	void handle_command(std::unique_ptr<kdebugger::process> & process, 
			std::string_view current_line) {
		
		auto args = split(current_line, ' ');
		auto command = args[0];

		if(is_prefix(command, "help")) {
			print_help();	
		}

		else if(is_prefix(command, "breakpoint")) {
			handle_breakpoint_command(*process, args);
		}
		
		else if(is_prefix(command, "continue")) {
			process->resume();
			auto reason = process->wait_on_signal();
			handle_stop(*process, reason);
		}
			
		else if(is_prefix(command, "step")) {
			auto reason = process->step_instruction();
			handle_stop(*process, reason);
		}
			
		else if(is_prefix(command, "memory")) {
			handle_memory_command(*process, args);
		}

		else {
			std::cerr << "> Unknown Command entered.\n";
		}
	}
}

namespace {

	void main_loop(std::unique_ptr<kdebugger::process> & process) {
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
					handle_command(process, line);
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
		auto process = attach(argc, argv);
		main_loop(process);
	}

	catch (const kdebugger::error & err) {
		std::cout << err.what() << '\n';
	}
}
