#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace kdebugger {

	using byte64 = std::array<std::byte, 8>;
	using byte128 = std::array<std::byte, 16>;

	class file_addr;
	class elf;

	class virt_addr {
		
		private:
			std::uint64_t m_Addr {0};

		public:
			virt_addr() = default;

			explicit virt_addr(std::uint64_t addr) : m_Addr {addr} {}

			std::uint64_t addr() const {
				return m_Addr;
			}

			// takes an elf object in virtual memory and converts it
			// to a file address with a file offset
			file_addr to_file_addr(const elf & obj) const;

			// operatpr overloads
			virt_addr operator + (std::uint64_t offset) const {
				return virt_addr(m_Addr + offset);
			}

			virt_addr operator - (std::uint64_t offset) const {
				return virt_addr(m_Addr - offset);
			}

			virt_addr & operator += (std::uint64_t offset) {
				m_Addr += offset;
				return *this;
			}

			virt_addr & operator -= (std::uint64_t offset) {
				m_Addr -= offset;
				return *this;
			}
			
			// can replace these eventually with <=>
			bool operator == (const virt_addr & rhs) const noexcept {
				return m_Addr == rhs.m_Addr;
			}

			bool operator != (const virt_addr & rhs) const noexcept {
				return m_Addr != rhs.m_Addr;
			}

			bool operator < (const virt_addr & rhs) const noexcept {
				return m_Addr < rhs.m_Addr;
			}	

			bool operator <= (const virt_addr & rhs) const noexcept {
				return m_Addr <= rhs.m_Addr;
			}

			bool operator > (const virt_addr & rhs) const noexcept {
				return m_Addr > rhs.m_Addr;
			}

			bool operator >= (const virt_addr & rhs) const noexcept {
				return m_Addr >= rhs.m_Addr;
			}

			file_addr to_file_addr(const elf_collection & elves) const;
	};
	
	// span class for a pointer with a known length, similar to std::span
	// however, this will need to be more specialized to take a view of memory address'
	template <class T>
	class span {
		
		private:
			T* m_Data {nullptr};
			std::size_t m_Size {0};
		public:
			// default constructor && overloads
			span() = default;
			span(T* data, std::size_t size) : m_Data {data}, m_Size {size} {}
			span(T* data, T* end) : m_Data {data}, m_Size {end - data} {}
		
			template <class U>
			span(const std::vector<U> & vec) : m_Data {vec.data()}, m_Size {vec.size()} {}

			T* begin() const {
				return m_Data;
			}

			T* end() const {
				return m_Data + m_Size;
			}

			std::size_t size() {
				return m_Size;
			}

			T & operator [](std::size_t n) noexcept {
				return *(m_Data + n);
			}
	};

	class file_addr {
		
		private:
			const elf * m_Elf = nullptr;
			std::uint64_t m_Addr {0};

		public:
			file_addr() = default;

			file_addr(const elf & obj, std::uint64_t addr) : m_Elf {&obj}, m_Addr{addr} {}

			std::uint64_t addr() const {
				return m_Addr;
			}

			const elf * elf_file() const {
				return m_Elf;
			}
			
			// backwards-compatible -> conversion to a virtual address
			virt_addr to_virt_addr() const;

			file_addr operator + (std::uint64_t offset) const {
				return file_addr(*m_Elf, m_Addr + offset);
			}

			file_addr operator - (std::uint64_t offset) const {
				return file_addr(*m_Elf, m_Addr - offset);
			}

			file_addr & operator += (std::uint64_t offset) {
				m_Addr += offset;
				return *this;
			}

			file_addr & operator -= (std::uint64_t offset) {
				m_Addr -= offset;
				return *this;
			}

			bool operator == (const file_addr & other) const {
				return m_Addr == other.m_Addr && m_Elf == other.m_Elf;
			}

			bool operator != (const file_addr & other) const {
				return m_Addr != other.m_Addr && m_Elf != other.m_Elf;
			}

			bool operator < (const file_addr & other) const {
				assert(m_Elf == other.m_Elf);
				return m_Addr < other.m_Addr;
			}

			bool operator <= (const file_addr & other) const {
				assert(m_Elf == other.m_Elf);
				return m_Addr <= other.m_Addr;
			}

			bool operator > (const file_addr & other) const {
				assert(m_Elf == other.m_Elf);
				return m_Addr > other.m_Addr;
			}

			bool operator >= (const file_addr & other) const {
				assert(m_Elf == other.m_Elf);
				return m_Addr >= other.m_Addr;
			}
	};

	class file_offset {
		
		private:
			const elf * m_Elf = nullptr;
			std::uint64_t m_Offset {0};`

		public:
			file_offset() = delete;

			file_offset(const elf & obj, std::uint64_t offset) : m_Elf{obj}, m_Offset{offset} {}

			std::uint64_t off() const {
				return m_Offset;
			}

			const elf * elf_file() const {
				return m_Elf;
			}
	};
}
