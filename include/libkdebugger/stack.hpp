#pragma once

// general includes
#include <vector>

// Private / project specific includes
#include <libkdebugger/dwarf.hpp>
#include <libkdebugger/registers.hpp>
#include <libkdebugger/types.hpp>

namespace kdebugger {
	struct stack_frame {
		registers regs;
		virt_addr backtrace_report_address;
		die func_die;
		bool inlined = false;
		source_location location;
	};
	
	class target;

	class stack {
		
		private:
			target * m_Target = nullptr;
			std::uint32_t m_InlineHeight {0};
			std::vector<stack_frame> m_Frames;
			std::size_t m_CurrentFrame {0};

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
				m_CurrentFrame = m_InlineHeight;
			}

			void unwind();
			
			void up() {
				++m_CurrentFrame;
			}

			void down() {
				--m_CurrentFrame;
			}	

			span<const stack_frame> frames() const;
			
			bool has_frames() const {
				return !m_Frames.empty();
			}

			const stack_frame & current_frame() const {
				return m_Frames[m_CurrentFrame];
			}

			std::size_t current_frame_index() const {
				return m_CurrentFrame - m_InlineHeight;
			}

			const registers & regs() const;
			virt_addr get_pc() const;
	}
}
