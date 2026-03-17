#pragma once

// general includes
#include <vector>

// Private / project specific includes
#include <libkdebugger/dwarf.hpp>

namespace kdebugger {
	class target;

	class stack {
		
		private:
			target * m_Target = nullptr;
			std::uint32_t m_InlineHeight {0};

		public:
			stack(target * tgt) : m_Target {tgt} {}
			void reset_inline_height();
			std::vector<kdebugger::die> inline_stack_at_pc() const;
			
			std::uint32_t inline_height() {
				return m_InlineHeight;
			}

			const target & get_target() const {
				return *m_Target;
			}
			
			void simulate_inlined_step_in() {
				--m_InlineHeight;
			}
	}
}
