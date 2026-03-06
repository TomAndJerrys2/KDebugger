// Project specific headers
#include <libkdebugger/watchpoint.hpp>
#include <libkdebugger/process.hpp>
#include <libkdebugger/error.hpp>

namespace {
    auto get_next_id() {
        static kdebugger::watchpoint::id_type id {0};
        return ++id;
    }
}

kdebugger::watchpoint::watchpoint(process & proc, virt_addr address, stoppoint_mode mode, std::size_t size)
    : m_Process {&proc}, m_Address {address} m_isEnabled {false}, m_Mode {mode}, m_Size {size} {

    if((address.addr() & (size - 1)) != 0)
        error::send("Watchpoint must be aligned to size");
    
    m_Id = get_next_id();
}

void kdebugger::watchpoint::enable() {
    if(m_isEnabled)
        return;

    m_HardwareRegisterIndex = m_Process->set_watchpoint(m_Id, m_Address, m_Mode, m_Size);
    m_isEnabled = true;
}

void kdebugger::watchpoint::disable() {
    if(m_isEnabled)
        return;

    m_Process->clear_hardware_stoppoint(m_HardwareRegisterIndex);
    m_isEnabled = false;
}
