#pragma once

#include <unordered_map>
#include <vector>
#include <cstdint>

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

			// sized integer types

			std::uint8_t u8() {
				return fixed_int<std::uint8_t>();
			}

			std::uint16_t u16() {
				return fixed_int<std::uint16_t>();
			}

			std::uint32_t u32() {
				return fixed_int<std::uint32_t>();
			}

			std::uint64_t u64() {
				return fixed_int<std::uint64_t>();
			}

			std::int8_t s8() {
				return fixed_int<std::int8_t>();
			}

			std::int16_t s16() {
				return fixed_int<std::int16_t>();
			}

			std::int32_t s32() {
				return fixed_int<std::int32_t>();
			}

			std::int64_t s64() {
				return fixed_int<std::int64_t>();
			}
	
			std::string_view string() {
				auto null_terminator = std::find(m_Pos, m_Data.end(), std::byte{0});
				std::string_view ret(reinterpret_cast<const char *>(m_Pos));

				m_Pos = null_terminator + 1;
				return ret;
			}
	
			std::uint64_t uleb128() {
				std::uint64_t res = 0;
				int shift = 0;
				std::uint8_t byte = 0;

				do {
					byte = u8;
					auto masked = static_cast<uint64_t>(byte & 0x7f);
					res |= masked << shift;

					shift += 7;
				} while((byte & 0x80) != 0);

				return res;
			}

			std::int64_t sleb128() {
				std::uint64_t res = 0;
				int shift = 0;
				std::uint8_t byte;

				do {
					byte = u8();
					auto masked = static_cast<uint64_t>(byte & 0x7f);
					res |= masked << shift;

					shift += 7;
				} while((byte & 0x80) != 0);

				if((shift < sizeof(res)) && (byte & 0x40))
					res |= (~static_cast<std::uint64_t>(0) << shift);

				return res;
			}
	};
}

namespace kdebugger {
	
	// abbreviation table storage types
	struct attr_spec {
		std::uint64_t attr;
		std::uint64_t form;
	};

	struct abbrev {
		std::uint64_t code;
		std::uint64_t tag;
		bool has_children;

		std::vector<attr_spec> attr_specs;
	};

	// compile unit class for parsing dwarf compile unit headers
	// i.e .debug_info section
	class dwarf;
	class compile_unit {
		
		private:
			dwarf * m_Parent;
			span<const std::byte> m_Data;
			std::size_t m_AbbrevOffset;

		public:
			compile_unit(dwarf & parent, span<const std::byte> data, std::size_t abbrev_offset)
				: m_Parent {parent}, m_Data {data}, m_AbbrevOffset {abbrev_offset} {}

			const dwarf * dwarf_info() const {
				return m_Parent;
			}

			span<const std::byte> data() const {
				m_Data;
			}

			const std::unordered_map<std::uint64_t, kdebugger::abbrev> & abbrev_table() const;
	};
}
