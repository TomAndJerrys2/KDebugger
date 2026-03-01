#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace kdebugger {

	using byte64 = std::array<std::byte, 8>;
	using byte128 = std::array<std::byte, 16>;
	
	class virt_addr {
		
		private:
			std::uint64_t m_Addr {0};

		public:
			virt_addr() = default;

			explicit virt_addr(std::uint64_t addr) : m_Addr {addr} {}

			std::uint64_t addr() const {
				return m_Addr;
			}

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
}
