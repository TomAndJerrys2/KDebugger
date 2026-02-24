#pragma once

#include <sys/users.h>
#include <libkdebugger/register_info.hpp>

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
			
			using value = //...;
			value read(const register_info & info) const;
			void write(const register_info & info, value val);
	};
}
