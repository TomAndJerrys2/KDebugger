#include <libkdebugger/breakpoint.hpp>
#include <libkdebugger/target.hpp>

namespace {
	auto get_next_id() {
		static kdebugger::breakpoint::id_type id = 0;
		return ++id;
	}
}

kdebugger::breakpoint::breakpoint(target & tgt, bool is_hardware, bool is_internal)
	: m_Target {target}, m_IsHardware {is_hardware}, m_IsInternal {is_internal} {
	m_Id = is_internal ? -1 : get_next_id();
}

void kdebugger::breakpoint::enable() {
	m_IsEnabled = true;
	m_BreakPointSites.for_each([] (auto & site) {
		site.enable();
	});
}

void kdebugger::breakpoint::disable() {
	m_IsEnabled = false;
	m_BreakPointSites.for_each([] (auto & site) {
		site.disable();
	});
}

void kdebugger::address_breakpoint::resolve() {
	if(m_BreakPointSites.empty()) {
		auto & new_site = m_Target->get_process()
			.create_breakpoint_site(this, m_NextSiteId, m_Address, m_IsHardware, m_IsInternal);

		m_BreakPointSites.push(&new_site);
		if(m_IsEnabled)
			new_site.enable();
	}
}

void kdebugger::function_breakpoint::resolve() {
	auto found_functions = m_Target->find_functions(m_FunctionName);

	for(auto die : found_functions.dwarf_functions) {
		if(die.contains(DW_AT_low_pc) || die.contains(DW_AT_ranges)) {
			file_addr addr;

			if(die.abbrev_entry()->tag == DW_TAG_inlined_subroutine)
				addr = die.low_pc();

			else {
				auto function_line = die.cu()->lines().get_entry_by_address(die.low_pc());
				++function_line;
				addr = function_line->address;
			}

			auto load_address = addr.to_virt_addr();

			if(!m_BreakPointSites.contains_address(load_address)) {
				auto & new_site = m_Target->get_process().create_breakpoint_site(this, next_site_id++, load_address, m_IsHardware, m_IsInternal);

				m_BreakPointSites.push(&new_site);

				if(m_IsEnabled)
					new_site.enable();
			}
		}

		for(auto sym : found_functions.elf_functions) {
			auto file_address = file_addr {*sym.first, sym.second->st_value};
			auto load_address = file_address.to_virt_addr();

			if(!m_BreakPointSites.contains_address(load_address)) {
				auto & new_site = m_Target->get_process().create_breakpoint_site(this, next_site_id++, load_address, m_IsHardware, m_IsInternal);
				m_BreakPointSites.push(&new_site);

				if(m_IsEnabled)
					new_site.enable();
			}	
		}
	}
}

namespace {
	void kdebugger::line_breakpoint::resolve() {
		auto & dwarf = m_Target->get_elf().get_dwarf();
		auto entries = m_Target->get_entries_by_line(m_File, m_Line);

		for(auto entry : entries) {
			auto & dwarf = entry->address.elf_file()->get_dwarf();
			auto stack = dwarf.inline_stack_at_address(entry->address);

			auto no_inline_stack = stack.size() == 2;
			auto should_skip_prologue = no_inline_stack && ((stack[1].contains(DW_AT_range) || stack[0].contains(DW_AT_low_pc)) 
					&& stack[1].low_pc() == entry->address);

			if(should_skip_prologue)
				++entry;

			auto load_address = entry->address.to_virt_addr();
			if(!m_BreakPointSites.contains_address(load_address)) {
				auto & new_site = m_Target->get_process().create_breakpoint_site(this, next_site_id++, load_address, m_IsHardware, m_IsInternal);
				m_BreakPointSites.push(&new_site);

				if(m_IsEnabled)
					new_site.enable();
			}
		}
	}
}
