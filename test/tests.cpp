#include <sys/types.h>
#include <signal.h>
#include <fstream>

#include <catch2/catch_test_macros.hpp>
#include <libkdebugger/process.hpp>
#include <libkdebugger/pipe.hpp>
#include <libkdebugger/bit.hpp>

using namespace kdebugger;

namespace {
	
	bool process_exists(const pid_t pid) {
		auto ret = kill(pid, 0);
		return (ret != -1 && errno != ESRCH);
	}

	char get_process_status(const pid_t pid) {
		std::ifstream stat("/proc/" + std::to_string(pid) + "/stat");
		std::string data;
		std::getline(stat, data);

		auto index_of_last_parenthesis = data.rfind(')');
		auto index_of_status_indicator = index_of_last_parenthesis + 2;
		return data[index_of_status_indicator];
	}
	
	// gets section load bias via file offset
	std::uint64_ t get_section_load_bias(std::filesystem::path path, Elf64_addr file_address) {
		auto command = std::string("readelf -WS") + path.string();
		auto pipe = popen(command.c_str(), "r");

		std::regex text_regex(R"(PROGBITS\s+(\w+)\s+(\w+)\s+(\w+))");
		char* line = nullptr;
		std::size_t len {0};

		while(getline(&line, &len, pipe)) {
			std::cmatch groups;
			
			if(std::regex_search(line, groups, text_regex)) {
				auto address = std::stol(groups[1], nullptr, 16);
				auto offset = std::stol(groups[2], nullptr, 16);
				auto size = std::stol(groups[3], nullptr, 16);

				if(address <= file_address && file_address < (address + size)) {
					free(line);
					pclose(pipe);
					return address - offset;
				}
			}
		
			free(line);
			line = nullptr;
		}

		pclose(pipe);
		kdebugger::error::send("Could not find section load bias\n");
	}
}

// in case - tests if the program launched exists
// as a process and therefore can be halted/resumed
TEST_CASE("process::launch success", "[process]") {
	auto proc = process::launch("yes");
	REQUIRE(process_exists(proc->pid));
}

// in case - program passed to attach does not exist
TEST_CASE("process::launch no such program", "[process]") {
	REQUIRE_THROW_AS(process::launch("example_program"), error);
}

// in case - if the process in procfs has stopped being traced
TEST_CASE("process::attach success", "[process]") {
	// launches a program without attaching
	auto pid {};
	auto proc = process::attach(pid);
	REQUIRE(get_process_status(pid) == 't');
}

// in case - checking we aren't attaching to an already
// attached process or program
TEST_CASE("process::attach success", "[process]") {
	auto target = process::launch("targets/run_endlessly", false);
	auto proc = process::attach(target->pid());
	REQUIRE(get_process_status(target->pid()) == 't');
}

// in case - an invalid PID is trying to be attached to
TEST_CASE("process::attach invalid PID", "[process]") {
	REQUIRE_THROW_AS(process::attach(0), error);
}

// in case - fundamentally testing pre-existing and forked
// process that may be launched by the debugger
TEST_CASE("process::resume success", "[process]") {
	// where launch debug = true
	{
		auto proc = process::launch("targets/run_endlessly");
		proc->resume();

		auto status = get_process_status(proc->pid());
		auto success = (status == 'R' || status == 'S');
		REQUIRE(success);
	}
	
	// where launch debug = false
	{
		auto target = process::launch("targets/run_endlessly", false);
		proc->resume();

		auto status = get_process_status(proc->pid());
		auto success = (status == 'R' || status == 'S');
		REQUIRE(success);
	}
}

// in case - check if a program ends immediately
TEST_CASE("process::resume already terminated", "[process]") {
	auto proc = process::launch("targets/end_immediately");
	proc->resume();
	proc->wait_on_signal();

	REQUIRE_THROW_AS(proc->resume(), error);
}

// case if -checks if writing to a given register works
TEST_CASE("Write register works", "[register]") {
	bool close_on_exec = false;
	kdebugger::pipe channel(close_on_exec);

	auto proc = process::launch(
			"targets/reg_write", true, channel.get_write()
	);
	channel.close_write();

	proc->resume();
	proc->wait_on_signal();
	
	// test case for data integrity via IPC
	auto & regs = proc->get_registers();
	regs.write_by_id(register_id::rsi, 0xcafecafe);

	proc->resume();
	proc->wait_on_signal();

	auto output = channel.read();
	REQUIRE(to_string_view(output) == "0xcafecafe");
	
	// test case for MMX registers
	regs.write_by_id(register_id::mm0, 0xba5eba11);
	proc->resume();
	proc->wait_on_signal();

	output = channel.read();
	REQUIRE(to_string_view(output) == "0xba5eba11");
	
	// test case for SSE registers
	regs.write_by_id(register_id::xmm0, 42.24);
	proc->resume();
	proc->wait_on_signal();

	output = channel.read();
	REQUIRE(to_string_view(output) == "42.24");
	
	// test case for x87 space registers 
	// i.e long-double precision units
	regs.write_by_id(register_id::st0, 42.23l);
	regs.write_by_id(register_id::fsw, std::uint16_t {0b0011100000000000});
	regs.write_by_id(register_id::ftw, std::uint16_t {0b0011111111111111});

	proc->resume();
	proc->wait_on_signal();

	output = channel.read();
	REQUIRE(to_string_view(output) == "42.24");
}

TEST_CASE("Read register works", "[register]")  {
	auto proc = process::launch("targets/reg_read");
	auto & regs = proc->get_registers();

	proc->resume();
	proc->wait_on_signal();

	REQUIRE(regs.read_by_id_as<std::uint64_t> (register_id::r13) 
			== 0xcafecafe);

	proc->resume();
	proc->wait_on_signal();

	REQUIRE(regs.read_by_id_as<std::uint8_t> (register_id::r13b) 
			== 42);

	proc->resume();
	proc->wait_on_signal();
	

	REQUIRE(regs.read_by_id_as<byte64> (register_id::mm0)
			== to_byte64(0xba5eb11ull));

	proc->resume();
	proc->wait_on_signal();
	
	// we use dyadic rationals here, reading bit representations
	// of floating-point values is somewhat tricky. However exponents
	// of powers of 2 are easily represented hence the .125 here

	REQUIRE(regs.read_by_id_as<byte128> (register_id::mm0) 
			== to_byte128(64.125));

	proc->resume();
	proc->wait_on_signal();

	REQUIRE(regs.read_by_id_as<long double> (register_id::st0)
			== 64.125L);
}

// in case - checking if a breakpoint site can be created given a virtual address
TEST_CASE("Can create a breakpoint site", "[breakpoint]") {
	auto proc = process::launch("targets/run_endlessly");
	auto & site = proc->create_breakpoint_site(virt_addr{42});
	REQUIRE(site.address().addr() == 42);
}

// in case - checking if multiple breakpoint sites can be created
// at multiple virtual address spaces
TEST_CASE("breakpoint site ids increase", "[breakpoint]") {
	auto proc = process::launch("targets/run_endlessly");

	auto & s1 = proc->create_breakpoint_site(virt_addr{42});
	REQUIRE((s1.address().addr() == 42);

	auto & s2 = proc->create_breakpoint_site(virt_addr{43});
	REQUIRE((s2.address().addr() == 43);

	auto & s3 = proc->create_breakpoint_site(virt_addr{44});
	REQUIRE((s3.address().addr() == 44);
	
	auto & s4 = proc->create_breakpoint_site(virt_addr{45});
	REQUIRE((s4.address().addr() == 45);
}

// in case - a breakpoint site can be found and/or
// has been successful to create
TEST_CASE("Can find breakpoint site", "[breakpoint]") {
	auto proc = process::launch("targets/run_endlessly");
	const auto & cproc = proc;
	
	proc->create_breakpoint_site(virt_addr{42});
	proc->create_breakpoint_site(virt_addr{43});
	proc->create_breakpoint_site(virt_addr{44});
	proc->create_breakpoint_site(virt_addr{45});
	
	// testing non-constness of our breakpoint sites
	auto & s1 = proc->breakpoint_sites().get_by_address(virt_addr{44});
	REQUIRE(proc->breakpoint_sites().contains_address(virt_addr{44}));
	REQUIRE(s1.address().addr() == 44);
	
	// testing constness of our breakpoint sites
	auto & cs1 = cproc->breakpoint_sites().get_by_address(virt_addr{44});
	REQUIRE(cproc->breakpoint_sites().contains_address(virt_addr{45}));
	REQUIRE(cs1.address().addr() == 44);
	
	// testing non-constness of our ids assigned to breakpoints
	auto & s2 = proc->breakpoint_sites().get_by_id(s1.id() + 1);
	REQUIRE(proc->breakpoint_sites().contains_id(s1.id() + 1));
	REQUIRE(s2.id() == s1.id() + 1);
	REQUIRE(s2.address().addr() == 45);

	// testing constness of our ids assigned to constant breakpoints
	auto & cs2 = proc->breakpoint_sites().get_by_id(cs1.id() + 1);
	REQUIRE(cproc->breakpoint_sites().contains_id(cs1.id() + 1));
	REQUIRE(cs2.id() == cs1.id() + 1);
	REQUIRE(cs2.address().addr() == 45);
}

// in case - if a breakpoint site could not be found and/or
// has been unsuccessful in creation
TEST_CASE("Cannot find breakpoint site", "[breakpoint]") {
	auto proc = process::launch("targets/run_endlessly");
	const auto & cproc = proc;

	REQUIRE_THROW_AS(proc->breakpoint_sites().get_by_address(virt_addr{44}), error);
	REQUIRE_THROW_AS(proc->breakpoint_sites().get_by_id(44), error);
	REQUIRE_THROW_AS(cproc->breakpoint_sites().get_by_address(virt_addr{44}), error);
	REQUIRE_THROW_AS(cproc->breakpoint_sites().get_by_id(44), error);
}

// in case - tests the breakpoint site vector for size and emptiness
TEST_CASE("Breakpoint site list size and emptiness", "[breakpoint]") {
	auto proc = process::launch("targets/run_endlessly");
	const auto & cproc = proc;

	REQUIRE(proc->breakpoint_sites().empty());
	REQUIRE(proc->breakpoint_sites().size() == 0);
	REQUIRE(cproc->breakpoint_sites().empty());
	REQUIRE(cproc->breakpoint_sites().size() == 0);
	
	proc->create_breakpoint_site(virt_addr{42});
	REQUIRE(!proc->breakpoint_sites().empty());
	REQUIRE(proc->breakpoint_sites().size() == 1);
	REQUIRE(!cproc->breakpoint_site().empty());
	REQUIRE(cproc->breakpoint_sites().size() == 1);
	
	proc->create_breakpoint_site(virt_addr{43});
	REQUIRE(!proc->breakpoint_sites().empty());
	REQUIRE(proc->breakpoint_sites().size() == 2);
	REQUIRE(!cproc->breakpoint_sites().empty());
	REQUIRE(cproc->breakpoint_sites().size() == 2);
}

// in case - is able to iterate over the breakpoint sites vectoc
TEST_CASE("Can iterate breakpoint sites", "[breakpoint]") {
	auto proc = process::launch("targets/run_endlessly");
	const auto & cproc = proc;

	proc->create_breakpoint_site(virt_addr{42});
	proc->create_breakpoint_site(virt_addr{43});
	proc->create_breakpoint_site(virt_addr{44});
	proc->create_breakpoint_site(virt_addr{45});

	proc->breakpoint_sites().for_each(
		[addr = 42] (auto & site) mutable {
			REQUIRE(site.address().addr() == addr++);
		}
	);

	cproc->breakpoint_sites().for_each(
		[addr = 42] (auto & site) mutable {
			REQUIRE(site.address().addr() == addr++);
		}
	);
}
