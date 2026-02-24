#pragma once

// General headers
#include <sys/users.h>
#include <variant>

// Private / project-specific headers
#include <libkdebugger/register_info.hpp>
#include <libkdebugger/types.hpp>

namespace kdebugger {

	class process;
	class registers {
	
		private:
			
			friend process;
			
			// from sys/users.h
			user m_Data;
			process * m_Process
			
			registers(process & proc) : m_Process {proc} {} 

		public:
			// delete default constructor
			registers() = delete;

			// delete copy constructor && copy assignment
			registers(const registers &) = delete;
			registers & operator=(const registers &) = delete;
			
			// std::variant for better type-safety around unions
			using value = std::variant <
				std::uint8_t, std::uint16_t, std::uint32_t, std::uint64_t,
				std::int8_t, std::int16_t, std::int32_t, std::int64_t,
				float, double, long double, byte64, byte128
			>;
			
			value read(const register_info & info) const;
			void write(const register_info & info, value val);
	};
}
