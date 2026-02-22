// General Headers
#include <unistd.h>
#include <fcntl.h>
#include <utility>

// Private / Project-specific headers
#include <libkdebugger/pipe.hpp>
#include <libkdebugger/error.hpp>

// Wrapper constructor for pipe2
kdebugger::pipe::pipe(bool close_on_exec) {
	if(pipe2(m_Fds, close_on_exec ? O_CLOEXEC : 0) < 0) {
		error::send_errno("Failed to create pipe");
	}
}

// release the read buffer, exchanging for -1
constexpr int kdebugger::pipe::release_read() noexcept {
	return std::exchange(m_Fds[read_fd], -1);
}

// release the write buffer, exchanging for -1
constexpr int kdebugger::pipe::release_write() noexcept {
	return std::exchange(m_Fds[write_fd], -1);
}

void kdebugger::pipe::close_read() const {
	if(m_Fds[read_fd] != -1) {
		close(m_Fds[read_fd]);
		m_Fds[read_fd] = -1;
	}
}

void kdebugger::pipe::close_write() const {
	if(m_Fds[write_fd] != -1) {
		close(m_Fds[write_fd]);
		m_Fds[write_fd] = -1;
	}
}

// destructor handles closing the read and write buffers
// stored as indexes in m_Fd
kdebugger::pipe::~pipe() {
	close_read();
	close_write();
}
