// General header includes
#include <sys/ptrace.h>

// Private / project-specific headers
#include <libkdebugger/breakpoint_site.hpp>
#include <libkdebugger/process.hpp>
#include <libkdebugger/error.hpp>

namespace {
	
	auto get_next_id() {
		static kdebugger::breakpoint_site::id_type id = 0;
		return ++id;
	}
}

// private constructor definition
kdebugger::breakpoint_site::breakpoint_site(process & proc, virt_addr address, bool is_hardware, bool is_internal)
	: m_Process {&proc}, m_Address {address}, m_isEnabled {false}, m_SavedData {}, m_isHardware {is_hardware},
		m_isInternal {is_internal} {
	
	m_Id = m_isInternal ? -1 : get_next_id();
}

// replaces instruction with int3 instruction encoded as 0xcc
void kdebugger::breakpoint_site::enable() {
	if(m_isEnabled)
		return;

	errno = 0;
	std::uint64_t data = ptrace(PTRACE_PEEKDATA, m_Process->pid(), m_Address, nullptr);
	if(errno != 0)
		error::send_errno("Enabling breakpoint site failed!\n");

	m_SavedData = static_cast<std::byte>(data & 0xff);
	std::uint64_t int3 = 0xcc;
	std::uint64_t data_with_int3 = ((data & ~0xff) | int3);
	if(ptrace(PTRACE_POKEDATA, m_Process->pid(), m_Address, data_with_int3) < 0)
		error::send_errno("Enabling breakpoint site failed\n");

	m_isEnabled = true;
}

// disables a break point poking our restored data before in3
void kdebugger::breakpoint_site::disable() {
	if(!m_isEnabled)
		return;

	errno = 0;
	std::uint64_t data = ptrace(PTRACE_PEEKDATA, m_Process->pid(), m_Address, nullptr);
	if(errno != 0)
		error::send_errno("Disabling breakpoint site failed\n");

	auto restored_data = ((data & ~0xff) | static_cast<std::uint8_t>(m_SavedData));
	if(ptrace(PTRACE_POKEDATA, m_Process->pid(), m_Address, restored_data) < 0)
		error::send_errno("Disabling breakpoint site failed\n");

	m_isEnabled = false;
}
