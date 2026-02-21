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

namespace {
	
	// attachs to a currently running process ID
	// based on the command-line argument passed
	pid_t attach(int argc, const char** argv) const {
		pid_t pid = 0;
		
		// a PID is passed
		if(argc == 3 && argv[1] == std::string_view("-p")) {
			pid = std::atoi(argv[2]);

			if(pid <= 0) {
				std::cerr << "> Invalid PID passed,\n";
				return -1;
			}
			
			// using the ptrace API to attach to a process
			// the address and data which are void*'s are just
			// passed as nullptr (unused in PTRACE_ATTACH)
			if(ptrace(PTRACE_ATTACH, pid, nullptr, nullptr)) {
				std::perror("> Could not attach\n");
				return -1;
			}
		}
		// a Program name is passed
		else {
			const char* prog_path = argv[1];
			if((pid = fork()) < 0) {
				std::perror("> Failed to fork process.\n");
				return -1;
			}	
			
			// successfully forked, as it returns 0 in a
			// given child process
			if(pid == 0) {
				// checks if the process to be traced
				if(ptrace(PTRACE_TRACME, 0, nullptr, nullptr)) {
					std::perror("> Program Tracing failed.");
					return -1;
				}

				if(execlp(prog_path, prog_path, nullptr) < 0) {
					std::perror("> Exec failed.");
					return -1;
				}
			}
		}

		return pid;
	}
}

namespace {
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

}

namespace {
	// resumes the specified PID
	void resume(pid_t pid) {
		if(ptrace(PTRACE_CONT, pid, nullptr, nullptr) < 0) {
			std::cerr << "> Couldn't continue.\n";
			std::exit(-1);
		}	
	}

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
	// handles commands given by the command-line as arguments
	void handle_command(pid_t pid, std::string_view current_line) const {
		auto args = split(current_line, ' ');
		auto command = args[0];

		if(is_prefix(command, "continue")) {
			resume(pid);
			wait_on_signal(pid);
		}

		else {
			std::cerr << "> Unknown Command entered.\n";
		}
	}
}

// Main Execution
int main(int argc, const char** argv) {
		
	if(argc == 1) {
		std::cerr << "> No arguments were passed.\n";
		return -1;
	}

	const pid_t pid = attach(argc, argv);

	int wait_status;
	int options {0};

	if(waitpid(pid, &wait_status, options) < 0)
		std::perror("waitpid failed.\n");

	char* line {nullptr};
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

		if(!line_str.empty())
			// handles the command given to KDebugger
			handle_command(pid, line);
	}

	return EXIT_SUCCESS;
}
