#pragma once

// General includes
#include <memory>

// Project specific / private headers
#include <libkdebugger/elf.hpp>
#include <libkdebugger/process.hpp>

namespace kdebugger {
	class target {
		
		private:
			std::unique_ptr<process> m_Process;
			std::unique_ptr<process> m_Elf;

			target(std::unique_ptr<process> proc, std::unique_ptr<elf> obj) : m_Process {proc}, m_Elf {obj};

		public:
			target() = delete;
			target(const target &) = delete;
			target & operator=(const target &) = delete;

			static std::unique_ptr<target> launch(std::filesystem::path, std::optional<int> stdout_replacement = std::nullopt);
			static std::unique_ptr<target> attach(pid_t pid);

			process & get_process() {
				return *m_Process;
			}

			const process & get_process() const {
				return *m_Process;
			}

			elf & get_elf() {
				return *m_Elf;
			}

			const elf & get_elf() const {
				return *m_Elf;
			}
	};
}
