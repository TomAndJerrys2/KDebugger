#pragma once 

// general header includes
#include <optional>

// Private / project specific headers
#include <libkdebugger/process.hpp>

namespace kdebugger {
	
	class disassembler {
		
		struct instsruction {
			virt_addr address;
			std::string text;
		};
		
		private:
			process * m_Process {nullptr};

		public:
			disassembler(process & process) : m_Process {process} {}

			std::vector<instruction> disassemble(std::size_t n_Instructions, 
				std::optional<virt_addr> address = std::nullopt);
	};
}
