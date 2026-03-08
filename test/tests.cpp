#include <sys/types.h>
#include <signal.h>
#include <fstream>
#include <elf.h>
#include <regex>
#include <fcntl.h>

#include <catch2/catch_test_macros.hpp>
#include <libkdebugger/process.hpp>
#include <libkdebugger/pipe.hpp>
#include <libkdebugger/bit.hpp>
#include <libkdebugger/syscalls.hpp>

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

	virt_addr get_load_address(pid_t pid, std::int64_t offset) {
		std::ifstream maps("/proc/" + std::to_string(pid), + "/maps/");
		std::regex map_regex(R"((\w+)-\w+ ..(.). (\w+))");
		std::string data;

		while(std::getline(maps, data)) {
			std::smatch groups;
			std::regex_search(data, groups, map_regex);

			if(groups[2] == 'x') {
				auto low_range = std::stol(groups[1], nullptr, 16);
				auto file_offset = std::stol(groups[3], nullptr, 16);

				return virt_addr(offset - file_offset + low_range);
			}
		}

		kdebugger::error::send("Could not find load address");
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

// in case - checking if a breakpoint on a given address works
TEST_CASE("Breakpoint on address works", "[breakooint]") {
	bool close_on_exec = false;
	kdebugger::pipe channel(close_on_exec);

	auto proc = process::launch("targets/hello_kdebugger", true, channel.get_write());
	channel.close_write();

	auto offset = get_entry_point_offset("targets/hello_kdebugger");
	auto load_address = get_load_address(proc->pid(), offset);

	proc->create_breakpoint_site(load_address).enable();
	proc->resume();
	auto reason = proc->wait_on_signal();

	REQUIRE(reason.reason == process_state::stopped);
	REQUIRE(reason.info == SIGTRAP);
	REQUIRE(proc->get_pc() == load_address);

	proc->resume();
	reason = proc->wait_on_signal();

	REQUIRE(reason.reason == process_state::exited);
	REQUIRE(reason.info == 0);

	auto data = channel.read();
	REQUIRE(to_string_view(data) == "Hello, kdebugger!\n");
}

// in case - checking if a breakpoint site is removeable
TEST_CASE("Can remove breakpoint sites", "[breakpoint]") {
	auto proc = process::launch("targets/run_endlessly");

	auto & site = proc->create_breakpoint_site(virt_addr{42});
	proc->create_breakpoint_site(virt_addr{43});
	REQUIRE(proc->breakpoint_sites().size() == 2);

	proc->breakpoint_sites().remove_by_id(site.id());
	proc->breakpoint_sites().remove_by_address(virt_addr{43});
	REQUIRE(proc->breakpoint_sites().empty());
}

// in case - reading and writing to and from memory respectively works
TEST_CASE("Reading and writing memory works", "[memory]") {
	// -- reading from memory -- // 	
	bool close_on_exec {false};
	kdebugger::pipe channel(close_on_exec);
	auto proc = process::launch("targets/memory", true, channel.get_write());
	channel.close_write();

	proc->resume();
	proc->wait_on_signal();

	auto a_pointer = from_bytes<std::uint64_t>(channel.read().data());
	auto data_vec = proc->read_memory(virt_addr {a_pointer}, 8);
	auto data = from_bytes<std::uint64_t>(data_vec.data());

	REQUIRE(data == 0xcafecafe);

	// -- writing to memory -- //
	proc->resume();
	proc->wait_on_signal();

	auto b_pointer = from_bytes<std::uint64_t>(channel.read().data());
	proc->write_memory(virt_addr {b_pointer}, {as_bytes("Hello, KDebugger!"), 12});
	auto read = channel.read();

	REQUIRE(to_string_view(read) == "Hello, KDebugger!");
}

// in case - hardware breakpoints should evade memory checksums
TEST_CASE("Hardware breakpoint evads memory checksums", "[breakpoint]") {
    bool close_on_exec {false};
    kdebugger::pipe channel(close_on_exec);
    auto proc = process::launch("targets/anti_debugger", true, channel.get_write());

    channel.close_write();

    proc->resume();
    proc->wait_on_signal();

    auto func = virt_addr(from_bytes<std::uint64_t>(channel.read().data()));

    auto & soft = proc->create_breakpoint_site(func, false);
    soft.enable();

    proc->resume();
    proc->wait_on_signal();

    REQUIRE(to_string_view(channel.read()) == "Putting pepperoni on pizza");

    proc->breakpoint_sites().remove_by_id(soft.id());
    auto & hard = proc->create_breakpoint_site(func, true);
    hard.enable();

    proc->resume();
    proc->wait_on_signal();

    REQUIRE(proc->get_pc() == func);

    proc->resume();
    proc->wait_on_signal();

    REQUIRE(to_string_view(channel.read()) == "Putting pineapple on pizza");
}

// in case - a watchpoint detects a read
TEST_CASE("Watchpoint detects read", "[watchpoint]") {
    bool close_on_exec {false};
    kdebugger::pipe channel(close_on_exec);

    auto proc = process::launch("targets/anti_debugger", true. channel.get_write());
    channel.close_write();

    proc->resume();
    proc->wait_on_signal();

    auto func = virt_addr(from_bytes<std::uint64_t>(channel.read().data()));
    auto & watch = proc->create_watchpoint(func, kdebugger::stoppoint_mode::read_write, 1);
    watch.enable();

    proc->resume();
    proc->wait_on_signal();
    proc->step_instruction();

    auto & soft = proc->create_breakpoint_site(func, false);
    soft.enable();

    proc->resume();
    auto reason = proc->wait_on_signal();

    REQUIRE(reason.info == SIGTRAP);

    proc->resume();
    proc->wait_on_signal();

    REQUIRE(to_string_view(channel.read()) == "Putting pineapple on pizza");
}

// in case - check the syscalls.inc and stringifcation
// of ids and names works in our map
TEST_CASE("Syscall mapping works", "[syscall]") {
	REQUIRE(kdebugger::syscall_id_to_name(0) == "read");
	REQUIRE(kdebugger::syscall_name_to_id("read") == 0);
	REQUIRE(kdebugger::syscall_id_to_name(62) == "kill");
	REQUIRE(kdebugger::syscall_name_to_id("kill") == 62);
}

// in case - ensuring a process halts for catchpoints which are set
TEST_CASE("Syscall catchpoints work", "[catchpoint]") {
	auto dev_null = open("/dev/null", O_WRONGLY);
	auto proc = process::launch("targets/anti_debugger", true, dev_null);

	auto write_syscall = kdebugger::syscall_name_to_id("write");
	auto policy = kdebugger::syscall_catch_policy::catch_some({write_syscall});
	proc->set_syscall_catch_policy(policy);

	proc->resume();
	auto reason = proc->wait_on_signal();

	REQUIRE(reason.reason == kdebugger::process_state::stopped);
	REQUIRE(reason.info == SIGTRAP);
	REQUIRE(reason.trap_reason == kdebugger:trap_type::syscall);
	REQUIRE(reason.syscall_info->id == write_syscall);
	REQUIRE(reason.syscall_info->entry == true);

	proc->resume();
	reason = proc->wait_on_signal();

	REQUIRE(reason.reason == kdebugger::process_state::stopped);
	REQUIRE(reason.info == SIGTRAP);
	REQUIRE(reason.trap_reason == kdebugger::trap_type::syscall);
	REQUIRE(reason.syscall_info->id == write_syscall);
	REQUIRE(reason.syscall_info->entry == true);

	close(dev_null);
}
