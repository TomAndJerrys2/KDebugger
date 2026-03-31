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
			std::function<bool (void)> m_OnHit;

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

			void install_hit_handler(std::function<bool (void)> on_hit) {
				m_OnHit = std::move(on_hit);
			}

			bool notify_hit() const {
				if(m_OnHit)
					return m_OnHit();

				return false;
			}
	};

	class function_breakpoint : public breakpoint {
		
		public:
			void resolve() override;
			std::string_view function_name() const { return m_FunctionName; }
	
		private:
			friend target;
			function_breakpoint(target & tgt, std::string function_name, bool is_hardware = false, bool is_internal = false)
				: breakpoint(tgt, is_hardware, is_internal), m_FunctionName {function_name} {
				resolve();
			}

			std::string m_FunctionName;
	};

	class line_breakpoint : public breakpoint {
		
		public:
			void resolve() override;

			const std::filesystem::path file() const {
				return m_File;
			}

			std::size_t line() const {
				return m_Line;
			}

		private:
			friend target;
			line_breakpoint(target & tgt, std::filesystem::path file, std::size_t line, bool is_hardware = false, 
					bool is_internal) : breakpoint(tgt, is_hardware, is_internal), m_File {std::move(file)}, m_Line {line} {
				resolve();
			}

			std::filesystem::path m_File;
			std::size_t m_Line;
	};

	class address_breakpoint : public breakpoint {
	
		public:
			void resolve() override;

			virt_addr address() const {
				return m_Address;
			}

		private:
			friend target;
			address_breakpoint(target & tgt, virt_addr address, bool is_hardware = false, bool is_internal = false)
				: breakpoint(tgt, is_hardware, is_internal), m_Address {address} {
				resolve();
			}

			virt_addr m_Address;
	};
}

