#pragma once

// General include paths
#include <filesystem>
#include <elf.h>
#include <vector>
#include <string_view>
#include <unordered_map>
#include <optional>

// Project specific / private include paths
#include <libkdebugger/types.hpp>

namespace kdebugger {
	// defines the ELF specification we can use to parse executables,
	// static/dynamic libraries or core dumps
	class elf {
		
		private:
			int m_Fd;
			std::filesystem::path m_Path;
			std::size_t m_FileSize;
			std::byte * m_Data;
			Elf64_Ehdr m_Header;

			// load bias stored here
			virt_addr m_LoadBias

			// a vector of section headers
			std::vector<Elf64_Shdr> m_SectionHeaders;

			// parses individual section headers encountered
			void parse_section_headers();

			// builds a section map in virtual memory
			void build_section_map();

			// contains the section map along with given Elf64 section headers
			std::unordered_map<std::string_view, Elf64_Shdr *> m_SectionMap;

			// gets a string from the string table at a given index
			// i.e from .strtab or .dynstr (abbreviated section)
			std::string_view get_string(std::size_t index) const;

		public:
			elf(const std::filesystem::path & path);
		
			// deletes copy constructor / copy assignment
			elf(const elf &) = delete;
			elf & operator = (const elf &) = delete;

			std::filesystem::path path() const {
				return m_Path;
			}

			const Elf64_Ehdr & get_header() const {
				return m_Header;
			}

			// gets section name in the elf binary
			std::string_view get_section_name(std::size_t index) const;

			// returns an Elf64 section header which we are looking at
			std::optional<const Elf64_Shdr *> get_section(std::string_view name) const;

			// returns a span of the contents and an observed section
			span<const std::byte> get_section_contents(std::string_view name) const;

			// returns the given load bias of an ELF binary stored in a page
			virt_addr load_bias() const {
				return m_LoadBias;
			}

			void notify_loaded(virt_addr address) {
				m_LoadBias = address;
			}
	
			const Elf64_Shdr * get_section_containing_address(file_addr addr) const;
			const Elf64_Shdr * get_section_containing_address(virt_addr addr) const;

			std::optional<file_addr> get_section_start_address(std::string_view name) const;

			~elf();
	}
}


