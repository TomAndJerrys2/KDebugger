// General Headers
#include <iostream>
#include <unistd.h>
#include <string_view>
#include <string>
#include <vector>
#include <algorithm>
#include <sstream>

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
			return kdebugger::process::launch(program_path);
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
		
		std::cout << "Process: " << process.pid() << ' ';
		
		switch(reason.reason) {
			case kdebugger::process_state::exited:
				std::cout << "Exited with status: " << static_cast<int>(reason.info);
				break;

			case kdebugger::process_state::terminated:
				std::cout << "Terminated wtih signal: " << sigabbrev_np(reason.info);
				break;
			
			case kdebugger::process_state::stopped:
				std::cout <<  "Stopped with signal: " << sigabbrev_np(reason.info);
				break;
		}

		std::cout << std::endl;
	}

	// handles commands given by the command-line as arguments
	void handle_command(std::unique_ptr<kdebugger::process> & process, 
			std::string_view current_line) {
		
		auto args = split(current_line, ' ');
		auto command = args[0];

		if(is_prefix(command, "continue")) {
			process->resume();
			auto reason = process->wait_on_signal();
			process->wait_on_signal();
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
