#pragma once

#include <unordered_map>

// I will implement this later... whole lotta constants lmao
#include <libkdebugger/detail/dwarf.h>
#include <libkdebugger/types.hpp>

namespace kdebugger {
	
	class elf;

	class dwarf {
		
		const elf * m_Elf;

		std::unordered_map<std::size_t, std::unordered_map<std::uint64_t, abbrev>> m_AbbrevTables;

		public:
			dwarf(const elf & parent);
			const elf * elf_file() const { return m_Elf; }
	
			const std::unordered_map<std::uint64_t, abbrev> & get_abbrev_table(std::size_t offset);
	};

	class cursor {
		
		std::span<const std::byte> m_Data;
		const std::byte* m_Pos;

		public:
			explicit cursor(kdebugger::span<const std::byte> data) : m_Data {data}, m_Pos {data.begin()} {}

			cursor & operator ++ () {
				++m_Pos; 
				return *this; 
			}

			cursor & operator += (std::size_t size) {
				m_Pos += size;
				return *this;
			}

			const std::byte * positon() const {
				return m_Pos;
			}

			bool finished() const {
				return m_Pos >= m_Data.end();
			}

			template <class T>
			T fixed_int() {
				auto t = kdebugger::from_bytes<T>(m_Pos);
				m_Pos += sizeof(T);

				return t;
			}
	};
};
