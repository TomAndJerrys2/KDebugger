#pragma once

// generic header includes
#include <cstdint>
#include <cstddef>

// Private / Project specific headers
#include <libkdebugger/types.hpp>

namespace kdebugger {

	class process;

	class breakpoint_site {
		
		private:
			breakpoint_site(breakpoint * parent, id_type id, process & proc, virt_addr address. 
						bool is_hardware = false, bool is_internal = false);
			friend process;

			process * m_Process;
			virt_addr m_Address;
			std::uint32_t m_Id;
			int m_HardwareRegisterIndex {-1};			

			bool m_isEnabled;
			bool m_isHardware;
			bool m_isInternal;			

			std::byte m_SavedData;

			breakpoint * m_Parent {nullptr};

		public:
			// delete default constructor
			breakpoint_site() = delete;

			// delete copy constructor
			breakpoint_site(const breakpoint_site &) = delete;

			// delete copy assignment
			breakpoint_site & operator = (const breakpoint_site &) = delete;

			using id_type = std::uint32_t;
			id_type id() const {
				return m_Id;
			}

			void enable();
			void disable();
			
			// getter to return enabled state
			bool is_enabled() const {
				return m_isEnabled;
			}
			
			// getter to return address type
			virt_addr address() const {
				return m_Address;
			}
			
			// breakpoint sites address is at a virtual address
			bool at_address(virt_addr addr) const {
				return m_Address == addr;
			}

			// checks for breakpoint being in a virtual address range
			bool in_range(virt_addr low, virt_addr high) const {
				return low <= m_Address && high > m_Address;
			}
	};
}
