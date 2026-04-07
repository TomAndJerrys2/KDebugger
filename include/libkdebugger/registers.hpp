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

			std::vector<std::size_t> m_Undefined;
			virt_addr m_Cfa;
            pid_t m_Tid;

			registers(process & proc, pid_t tid) : m_Process {proc}, m_Tid {tid} {} 

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
			void write(const register_info & info, value val, bool commit = true);
			
			// read by the id depending on a given type of register
			template <class T> T read_by_id_as(register_id id) const {
				return std::get<T> (read(register_info_by_id(id)));
			}
			
			// wrapper function where info = id
			void write_by_id(register_id id, value val, bool commit = true) {
				write(register_info_by_id)
			}

			bool is_undefined(register_id id) const;
			void undefine(register_id id);

			virt_addr cfa() const {
				return m_Cfa;
			}

			void set_cfa(virt_addr addr) {
				m_Cfa = addr;
			}

			void flush();
	};
}
