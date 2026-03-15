#pragma once

#include <unordered_map>
#include <vector>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

// I will implement this later... whole lotta constants lmao
#include <libkdebugger/detail/dwarf.h>
#include <libkdebugger/types.hpp>

namespace kdebugger {
	
	class elf;
	class compile_unit;

	class dwarf {
		
		private:
			const elf * m_Elf;

			std::vector<std::unique_ptr<compile_unit>> m_CompileUnits
			std::unordered_map<std::size_t, std::unordered_map<std::uint64_t, abbrev>> m_AbbrevTables;

			void index() const;
			void index_die(const die & current) const;

			struct index_entry {
				const compile_unit * cu;
				const std::byte * pos;
			};

			mutable std::unordered_multimap<std::string, index_entry> m_FunctionIndex;

			dwarf & get_dwarf() { 
				return *m_Dwarf
			};

			const dwarf & get_dwarf() const { 
				return *m_Dwarf 
			};

		public:
			dwarf(const elf & parent);
			const elf * elf_file() const { return m_Elf; }
	
			const std::unordered_map<std::uint64_t, abbrev> & get_abbrev_table(std::size_t offset);
	
			const std::vector<std::unique_ptr<compile_unit>> & compile_units() const {
				return m_CompileUnits
			}

			const compile_unit * compile_unit_containing_address(file_addr address) const;
			std::optional<die> function_containing_address(file_addr address) const;

			std::vector<die> find_functions(std::string name) const;
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
	
			std::string_view string();
			std::uint64_t uleb128();
			std::int64_t sleb128();
	
			void skip_form(std::uint64_t form);
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

			file_addr low_pc() const;
			file_addr high_pc() const;

			bool contains_address(file_addr address) const;
	
			std::optional<std::string_view> name() const;
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

namespace kdebugger {
	class compile_unit;
	class die;

	class range_list {
		
		private:
			const compile_unit * m_Cu;
			span<const std::byte> m_Data;
			file_addr m_BaseAddress;

		public:
			range_list(const compile_unit * cu, span<const std::byte> data, file_addr base_address)
				: m_Cu {cu}, m_Data {data}, m_BaseAddress {base_address} {}
	
			struct entry {
				file_addr low;
				file_addr high;

				bool contains(file_addr addr) const {
					return low <= addr && addr < high;
				}
			}

			class iterator;
			iterator begin() const;
			iterator end() const;

			bool contains(file_addr address) const;
	};

	class range_list::iterator {
	
		private:
			const compile_unit * m_Cu {nullptr};
			span<const std::byte> m_Data {nullptr, nullptr};
			file_addr m_BaseAddress;
			const std::byte * m_Pos {nullptr};
			entry m_Current;

		public:
			using value_type = entry;
			using reference = const entry &;
			using pointer = const entry *;
			using difference_type = std::ptrdiff_t;
			using iterator_category = std::forward_iterator_tag;

			iterator(const compile_unit* cu, span<const std::byte> data, file_addr base_address);

			iterator() = default;
			iterator(const iterator &) = default;
			iterator & operator = (const iterator &) = default;

			const entry & operator * () const { return m_Current; }
			const entry * operator -> () const { return &m_Current; }

			bool operator == (iterator rhs) const {
				return m_Pos == rhs.m_Pos;
			}

			bool operator != (iterator rhs) const {
				return m_Pos != rhs.m_Pos;
			}

			iterator & operator ++ ();
			iterator operator ++ (int);
	};

	class attr {
		
		private:
			const compile_unit * m_Cu;
			std::uint64_t m_Type;
			std::uint64_t m_Form;
			const std::byte * m_Location;

		public:
			attr(const compile_unit * cu, std::uint64_t type, std::uint64_t form, const std::byte * location)	
				: m_Cu {cu}, m_Type {type}, m_Form {form}, m_Location {location} {}

			std::uint64_t name() const {
				return m_Type;
			}

			std::uint64_t form() const {
				return m_Form;
			}

			file_addr as_address() const;
			std::uint32_t as_section_offset() const;
			span<const std::byte> as_block() const;
			std::uint64_t as_int() const;
			std::string_view as_string() const;
			die as_reference() const;

			range_list as_range_list() const;
	}
}
