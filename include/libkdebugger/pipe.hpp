#pragma once

#include <vector>
#include <cstddef>

namespace kdebugger {
	
	// pipe class for Parent-child process communication
	class pipe {
		
		public:
			explicit pipe(bool close_on_exec);
			~pipe();

			constexpr int get_read() const {
				return m_Fds[read_fd];
			}

			constexpr int get_write() const {
				return m_Fds[write_fd];
			}
			
			constexpr int release_read() noexcept;
			constexpr int release_write() noexcept;

			void close_read() const;
			void close_write() const;

			// read and write from 64K buffer
			std::vector<std::byte> read();
			void write(std::byte * from, const std::size_t bytes);

		private:
			int m_Fds[2];
			static constexpr unsigned int read_fd {0};
		   	static constexpr unsigned int write_fd {0};	
	};
}
