// general includes aka Zydis
#include <Zydis/Zydis.h>

// project specific includes
#include <libkdebugger/disassembler.hpp>

std::vector<kdebugger::disassembler::instruction> kdebugger::disassembler::disassemble(std::size_t n_instructions, 
	std::optional<virt_addr> address) {
	
	std::vector<instruction> ret;
	ret.reserve(n_instructions);
}
