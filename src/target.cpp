#include <libkdebugger/target.hpp>
#include <libkdebugger/types.hpp>

namespace {

	std::unique_ptr<kdebugger::elf> create_loaded_elf(const kdebugger::process & proc, 
			const std::filesystem::path & path) {
		auto auxv = proc.get_auxv();
		auto obj = std::make_unique<kdebugger::elf>(path);

		obj->notify_loaded(kdebugger::virt_addr(auxv[AT_ENTRY] - obj->get_header().e_entry));
	
		return obj;
	}	
}
