// General Headers
#include <iostream>
#include <unistd.h>
#include <string_view>
#include <sys/ptrace.h>

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

		}

		return pid;
	}
};

// Main Execution
int main(int argc, const char** argv) {
	
	// if our arguments are one
	// i.e a -p is passed to specify a
	// Linux process ID
	if(argc == 1) {
		std::cerr << "> No arguments were passed\n";
		return -1;
	}	
	
	// attach to the current PID passed
	// and return it to this variable
	const pid_t pid = attach(argc, argv);

	return 0;
}
