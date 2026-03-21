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
