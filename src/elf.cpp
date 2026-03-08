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
}

kdebugger::elf::~elf() {
	munmap(m_Data, m_FileSize);
	close(m_Fd);
}
