// General Headers
#include <iostream>
#include <unistd.h>

// Private/Project-specific headers
#include <libkdebugger/libkdebugger.hpp>

namespace {
	
	// attachs to a currently running process ID
	// based on the command-line argument passed
	const pid_t attach(int argc, const char** argv);
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
	pid_t pid = attach(argc, argv);

	return 0;
}
