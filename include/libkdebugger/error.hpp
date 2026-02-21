#pragma once

#include <stdexcept>
#include <cstring>

namespace kdebugger {
	
	class error : public std::runtime_error {
		
		error(const std::string & msg) : std::runtime_error(msg);

		public:
			[[noreturn]]
			static void send(const std::string & msg) { throw error(msg); }

			[[noreturn]]
			static void send_errno(const std::string & prefix) {
				throw error(prefix + ": " + std::strerror(errno));
			}
	};
}
