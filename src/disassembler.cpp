// general includes aka Zydis
#include <Zydis/Zydis.h>

// project specific includes
#include <libkdebugger/disassembler.hpp>

// for disassembling instructions to x64 ASM using the Zydis library as an interface
std::vector<kdebugger::disassembler::instruction> kdebugger::disassembler::disassemble(std::size_t n_instructions, 
	std::optional<virt_addr> address) {
	
	std::vector<instruction> ret;
	ret.reserve(n_instructions);

	if(!address)
		address.emplace(m_Process->get_pc());

	auto code = process->read_memory(*address, n_instructions * 15);

	ZyanUSize offset = 0;
	ZydisDisassembledInstruction instr;

	while(ZYAN_SUCCESS(ZydisDisassembleATT(ZYDIS_MACHINE_MODE_LONG_64, 
		address->addr(), code.data() + offset, code.size() - offset, &instr)) 
		&& n_instructions > 0) {
		
		ret.push_back(instruction{*address, std::string{instr.text}});
		offset += instr.info.length;
		*address += instr.info.length;
		
		--n_instructions;
	}

	return ret;
}
