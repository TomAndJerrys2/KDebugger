// General Headers
#include <unistd.h>
#include <fcntl.h>

// Private / Project-specific headers
#include <libkdebugger/pipe.hpp>
#include <libkdebugger/error.hpp>

// Wrapper constructor for pipe2
kdebugger::pipe::pipe(bool close_on_exec) {
	if(pipe2(m_Fds, close_on_exec ? O_CLOEXEC : 0) < 0) {
		error::send_errno("Failed to create pipe");
	}
}


