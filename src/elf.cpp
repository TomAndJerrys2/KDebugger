// General include file paths
#include <sys/types.h>
#include <sys/mmap.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

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

kdebugger::elf::~elf() {
	munmap(m_Data, m_FileSize);
	close(m_Fd);
}
