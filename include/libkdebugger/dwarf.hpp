#pragma once

#include <unordered_map>
#include <vector>
#include <cstdint>
#include <memory>
#include <optional>

// I will implement this later... whole lotta constants lmao
#include <libkdebugger/detail/dwarf.h>
#include <libkdebugger/types.hpp>

namespace kdebugger {
	
	class elf;
	class compile_unit;

	class dwarf {
		
		const elf * m_Elf;

		std::vector<std::unique_ptr<compile_unit>> m_CompileUnits
		std::unordered_map<std::size_t, std::unordered_map<std::uint64_t, abbrev>> m_AbbrevTables;

		public:
			dwarf(const elf & parent);
			const elf * elf_file() const { return m_Elf; }
	
			const std::unordered_map<std::uint64_t, abbrev> & get_abbrev_table(std::size_t offset);
	
			const std::vector<std::unique_ptr<compile_unit>> & compile_units() const {
				return m_CompileUnits
			}
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
	
			void skip_form(std::uint64_t form) {
				switch(form) {
				
					case DW_FORM_data1:
					case DW_FORM_ref1:
					case DW_FORM_flag:
						m_Pos += 1;
						break;

					case DW_FORM_data2:
					case DW_FORM_ref2:
						m_Pos += 2;
						break;

					case DW_FORM_data4:
					case DW_FORM_ref4:
					case DW_FORM_ref_addr:
					case DW_FORM_sec_offset:
					case DW_FORM_strp:
						m_Pos += 4;
						break;

					case DW_FORM_sdata:
						sleb128();
						break;

					case DW_FORM_udata:
					case DW_FORM_ref_udata:
						uleb128();
						break;

					case DW_FORM_block1:
						m_Pos += u8();
						break;

					case DW_FORM_block2:
						m_Pos += u16();
						break;

					case DW_FORM_block4:
						m_Pos += u32();
						break;

					case DW_FORM_block:
					case DW_FORM_exprloc:
						m_Pos += uleb128();
						break;

					case DW_FORM_string:
						while(!finished() && *m_Pos != std::byte(0)) {
							++m_Pos;
						}

						++m_Pos;
						break;

					case DW_FORM_indirect:
						skip_form(uleb128());
						break;

					default:
						kdebugger::error::send("Unrecognized DWARF form");
				}
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
	
	// debugging information entry structures are represented like trees
	class die {
		
		private:
			const std::byte * m_Pos {nullptr};
			const compile_unit * m_Cu {nullptr};
			const abbrev * m_Abbrev {nullptr};
			const std::byte * m_Next {nullptr};

			std::vector<const std::byte *> m_AttrLocs;

		public:
			explicit die(const std::byte * next) : m_Next {next} {}

			die(const std::byte * pos, const compile_unit * cu, const abbrev * ab, std::vector<const std::byte *> attr_locs,
					const std::byte * next) : m_Pos {pos}, m_Cu {cu}, m_Abbrev {ab}, m_AttrLocs {std::move(attr_locs)}, m_Next {next} {}

			const compile_unit * cu() const {
				return m_Cu;
			}

			const abbrev * abbrev_entry() const {
				return m_Abbrev;
			}

			const std::byte * position() const {
				return m_Pos;
			}

			const std::byte * next() const {
				return m_Next;
			}

			class children_range;
			children_range children() const;

			bool contains(std::uint64_t attribute) const;
			attr operator [] (std::uint64_t attribute) const;
	}

	class die::children_range {
	
		private:
			die m_Die;

		public:
			class iterator {

				private:
					std::optional<die> m_Die;

				public:
				using value_type = die;
				using reference = const die &;
				using pointer = different_type = std::ptrdiff_t;
				using iterator_category = std::forward_iterator_tag;

				iterator() = default;
				iterator(const iterator &) = default;
				iterator & operator=(const iterator &) = default;

				explicit iterator(const die & die);
		
				const die & operator * () const {
					*m_Die;
				}

				const die * operator -> () const {
					return &m_Die.value();
				}

				iterator & operator ++ ();
				iterator operator ++ (int);

				bool operator == (const iterator & rhs) const;
				bool operator != (const iterator & rhs) const {
					return !(*this == rhs);
				}
			};

			iterator begin() const {
				if(m_Die.m_Abbrev->has_children) {
					return iterator {m_Die};}
				}

				return end();
			}

			iterator end() {
				return iterator {};
			}
	};

	class compile_unit {
		
		private:
			dwarf * m_Parent;
			span<const std::byte> m_Data;
			std::size_t m_AbbrevOffset;

		public:
			compile_unit(dwarf & parent, span<const std::byte> data, std::size_t abbrev_offset)
				: m_Parent {parent}, m_Data {data}, m_AbbrevOffset {abbrev_offset} {}

			die root() const;
			
			const dwarf * dwarf_info() const {
				return m_Parent;
			}

			span<const std::byte> data() const {
				m_Data;
			}

			const std::unordered_map<std::uint64_t, kdebugger::abbrev> & abbrev_table() const;
	};
}
