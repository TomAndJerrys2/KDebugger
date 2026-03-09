// General include file paths
#include <sys/types.h>
#include <sys/mmap.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <cxxabi.h>

// Private / project specific headers
#include <libkdebugger/elf.hpp>
#include <libkdebugger/error.hpp>
#include <libkdebugger/bit.hpp>

kdebugger::elf::elf(const std::filesystem::path & path) {
	m_Path = path;

	if((m_Fd = open(path.c_str(), O_RDONLY)) < 0)
		error::send_errno("Could not open ELF File");

	struct stat stats;
	if(fstat(m_Fd, &stats) < 0)
		error::send_errno("Could not retrieve ELF file stats");

	m_FileSize = stats.st_size;
	void * ret;

	if((ret = mmap(0, m_FileSize, PROT_READ, MAP_SHARED, m_Fd, 0)) == MAP_FAILED) {
		close(m_Fd);
		error::send_errno("Could not mmap ELF file");
	}

	m_Data = reinterpret_cast<std::byte*>(ret);
	std::copy(m_Data + sizeof(header), as_byes(m_Header));

	parse_section_headers();
	build_section_map();
	parse_symbol_table();
	build_symbol_maps();
}

void kdebugger::elf::parse_section_headers() {
	auto n_headers = m_Header.e_shnum;
	if(n_headers == 0 && m_Header.e_shentsize != 0)
		n_header = from_bytes<Elf64_Shdr>(m_Data + m_Header.e_shoff).sh_size;

	m_SectionHeaders.resize(m_Header.e_shnum);
	std::copy(
		m_Data + m_Header.e_shoff, m_Data + m_Header.e_shoff + sizeof(Elf64_shr) * m_Header.e_shnum,
		reinterpret_cast<std::byte*>(m_SectionHeaders.data());
	);
}

std::string_view kdebugger::elf::get_section_name(std::size_t index) const {
	auto & section = m_SectionHeader[m_Header.e_shstrndx];
	return {
		reinterpret_cast<char*>(m_Data) + section.sh_offset + index
	};
}

kdebugger::span<const std::byte> kdebugger::elf::get_section_contents() const {
	if(auto sect = get_section(); sect) {
		return {data + sect.value()->sh_offset, sect.value()->sh_size};
	}

	return {nullptr, std::size_t {0}};
}

void kdebugger::elf::build_section_map() {
	for(auto & section : m_SectionHeaders) {
		m_SectionMap[get_section_name(section.sh_name)] = &section;
	}
}

std::optional<const Elf64_Shdr *> kdebugger::elf::get_section(std::string_view name) const {
	if(m_SectionMap.count(name) == 0)
		return std::nullopt;

	return m_SectionMap.at(name);
}

std::string_view kdebugger::elf::get_string(std::size_t index) const {
	auto opt_strtab = get_section(".strtab");
	
	if(!opt_strtab) {
		opt_strtab = get_section(".dynstr");
		
		if(!opt_strtab)
			return "";
	}

	return {
		reinterpret_cast<char *>(m_Data) + opt_strtab.value()->sh_offset + index
	};
}

const Elf64_Shr * kdebugger::elf::get_section_containing_address(file_addr addr) const {
	if(addr.elf_file() != this)
		return nullptr;

	for(auto & section : m_SectionHeader) {
		if(section.sh_addr <= addr.addr() && section.sh_addr + section.sh_size > addr.addr())
			return &section;
	}

	return nullptr;
}

// overload that takes a virtual address over a file address offset
const Elf64_Shr * kdebugger::elf::get_section_containing_address(virt_addr addr) const {
	for(auto & section : m_SectionHeaders) {
		if(m_LoadBias + section.sh_addr <= addr && m_LoadBias + section.sh_addr + section.sh_size > addr)
			return &section;
	}

	return nullptr;
}

std::optional<kdebugger::file_addr> kdebugger::elf::get_section_start_address(std::string_view name) const {
	if(auto sect = get_section(name); sect)
		return file_addr {*this, sect.value()->sh_addr};

	return std::nullopt;
}

void kdebugger::elf::parse_symbol_table() {
	auto opt_symtab = get_section(".symtab");

	if(!opt_symtab) {
		opt_symtab = get_section(".dynsym");

		if(!opt_symtab)
			return;
	}

	auto symtab = *opt_symtab;
	m_SymbolTable.resize(symtab->sh_size / symtab->sh_entsize);
	std::copy(m_Data + symtab->sh_offset, m_Data + symtab->sh_offset + symtab->sh_size,
			reinterpret_cast<std::byte*>(m_SymbolTable.data()));
}

void kdebugger::elf::build_symbol_maps() {
	for(auto & symbol : m_SymbolTable) {
		auto mangled_name = get_string(symbol.st_name);
		int demangled_status;
		auto demangled_name = abi::__cxa_demangle(mangled_name.data(), nullptr, nullptr, &demangle_status);

		if(demangled_status == 0) {
			m_SymbolNameMap.insert({demangled_name, &symbol});
			free(demangled_name);
		}

		m_SymbolNameMap.insert({mangled_name, &symbol});

		if(symbol.st_value != 0 && symbol.st_name != 0 && ELF64_ST_TYPE(symbol.st_info) != STT_TLS) {
			auto addr_range = std::pair(file_addr{*this, symbol.st_value}, file_addr{*this, symbol.st_value + symbol.st_size});

			m_SymbolAddrMap.insert({addr_range, &symbol});
		}
	}
}

std::vector<const Elf64_Sym *> kdebugger::elf::get_symbols_by_name(std::string_view name) const {
	auto [begin, end] = m_SymbolNameMap.equal_range(name);

	std::vector<const Elf64_Sym *> ret;
	std::transform(begin, end, std::back_inserter(ret), [](auto & pair) {
		return pair.second;
	});

	return ret;
}

std::optional<const Elf64_Sym *> kdebugger::elf::get_symbol_at_address(file_addr address) const {
	if(address.elf_file() != this)
		return std::nullopt;

	file_addr null_addr;
	auto it = m_SymbolAddrMap.find({address, null_addr});
	if(it == end(m_SymbolAddrMap))
		return std::nullopt;

	return it->second;
}

std::optional<const Elf64_Sym *> kdebugger::elf::get_symbol_at_address(virt_addr address) const {
	return get_symbol_at_address(address.to_file_addr(*this));
}

kdebugger::elf::~elf() {
	munmap(m_Data, m_FileSize);
	close(m_Fd);
}
