#pragma once

// General Includes
#include <cstdint>
#include <cstddef>
#include <string>

// Private / project specific includes
#include <libkdebugger/stoppoint_collection.hpp>
#include <libkdebugger/breakpoint_site.hpp>
#include <libkdebugger/types.hpp>

namespace kdebugger {
	class target;

	class breakpoint {
		
		protected:
			friend target;

			breakpoint(target & tgt, bool is_hardware = false, bool is_internal = false);
			
			id_type m_Id;
			target * m_Target;
			bool m_IsEnabled {false};
			bool m_IsHardware {false};
			bool m_IsInternal {false};

			breakpoint_site::id_type m_NextSiteId = 1;

		public:
			virtual ~breakpoint() = default;

			breakpoint() = delete;
			breakpoint(const breakpoint &) = delete;
			breakpoint & operator = (const breakpoint &) = delete;

			using id_type = std::int32_t;
			id_type id() const {
				return m_Id;
			}

			void enable();
			void disable();

			bool is_enabled() const {
				return m_IsEnabled;
			}

			bool is_hardware() const {
				return m_IsHardware;
			}

			bool is_internal() const {
				return m_IsInternal;
			}

			virtual void resolve() const = 0;

			stoppoint_collection<breakpoint_site, false> &  breakpoint_sites() { 
				return m_BreakPointSites; 
			}

			const stoppoint_collection<breakpoint_site, false> & breakpoint_sites() const {
				return m_BreakPointSites;
			}

			bool at_address(virt_addr addr) const {
				return m_BreakPointSites.contains_address(addr);
			}

			bool in_range(virt_addr low, virt_addr high) const {
				return m_BreakPointSites.get_in_region(low, high).empty();
			}
	};
}

