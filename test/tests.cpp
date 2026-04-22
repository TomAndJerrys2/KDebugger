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
#include <libkdebugger/dwarf.hpp>

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

// in case - testing the elf parser I wrote works!
TEST_CASE("ELF Parser works", "[elf]") {
	auto path = "targets/hello_sdb";
	kdebugger::elf elf(path);

	auto entry = elf.get_header().e_entry;
	auto sym = elf.get_symbol_at_address(file_addr{elf, entry});
	auto name = elf.get_string(sym.value()->st_name);

	REQUIRE(name == "_start");

	auto syms = elf.get_symbols_by_name("_start");
	name = elf.get_string(syms.at(0)->st_name);

	REQUIRE(name == "_start");
}

// in case - testing the syntax of DWARF is recognised
// by my interpreter impl
TEST_CASE("Correct DWARF language", "[dwarf]") {
	auto path = "targets/hello_kdebugger";
	kdebugger::elf elf(path);
	auto & compile_units = elf.get_dwarf().compile_units();

	REQUIRE(compile_units.size() == 1);

	auto & cu = compile_units[0];
	auto lang = cu->root()[DW_AT_language].as_int();

	REQUIRE(lang = DW_LANG_C_plus_plus);
}

// in case - able to iterate through dwarf debug info
TEST_CASE("Iterate DWARF", "[dwarf]") {
	auto path = "targets/hello_kdebugger";
	kdebugger::elf elf(path);
	auto & compile_units = elf.get_dwarf().compile_units();

	REQUIRE(compile_units.size() == 1);

	auto & cu = compile_units[0];
	std::size_t count = 0;

	for(auto & d : cu->root().children()) {
		auto a = d.abbrev_entry();
		REQUIRE(a->code != 0);
		++count;
	}

	REQUIRE(count > 0);
}

// in case - is able to find an entry-point function
// in a single/linked compile unit
TEST_CASE("Find main", "[dwarf]") {
	auto path = "targets/multi_cu";
	kdebugger::elf elf(path);
	kdebugger::dwarf dwarf(elf);

	bool found = false;
	for(auto & cu : dwarf.compile_units()) {
		for(auto & die : cu->root().children()) {
			if(die.abbrev_entry()->tag == DW_TAG_subprogram 
					&& die.contains(DW_AT_name)) {
				auto name = die[DW_AT_NAME].as_string();

				if(name == "main")
					found = true;
			}
		}
	}
}

// in case - range list can be iterated over
TEST_CASE("Range list", "[dwarf]") {
	auto path = "targets/hello_kdebugger";
	kdebugger::elf elf(path);
	kdebugger::dwarf dwarf(elf);
	auto & cu = dwarf.compile_units()[0];

	std::vector<std::uint64_t> range_data {
		0x12341234, 0x12341236,
		~0ULL, 0x32, 0x12341234,
		0x12341236, 0x0, 0x0
	};

	auto bytes = reinterpret_cast<std::byte *> (range_data.data());
	kdebugger::range_list list(cu.get(), {bytes, bytes + range_data.size()}, file_addr {});

	auto it = list.begin();
	auto e1 = *it;
	REQUIRE(e1.low.addr() == 0x12341234);
	REQUIRE(e1.high.addr() == 0x12341236);
	REQUIRE(e1.contains(file_addr {elf, 0x12341234}));
	REQUIRE(e1.contains(file_addr {elf, 0x12341235}));
	REQUIRE(!e1.contains(file_addr {elf, 0x12341236}));

	++it;
	auto e2 = *it;
	REQUIRE(e2.low.addr() == 0x12341266);
	REQUIRE(e2.high.addr() == 0x12341268);
	REQUIRE(e2.contains(file_addr {elf, 0x12341266}));
	REQUIRE(e2.contains(file_addr {elf, 0x12341267}));
	REQUIRE(!e2.contains(file_addr {elf, 0x12341269}));

	++it;
	REQUIRE(it == list.end());
	REQUIRE(list.contains(file_addr {elf, 0x12341234}));
	REQUIRE(list.contains(file_addr {elf, 0x12341235}));
	REQUIRE(!list.contains(file_addr {elf, 0x12341236}));
	REQUIRE(list.contains(file_addr {elf, 0x12341266}));
	REQUIRE(list.contains(file_addr {elf, 0x12341267}));
	REQUIRE(!list.contains(file_addr {elf, 0x12341268}));
}

// in case -check the main dwarf parser indeed functions 
// as expected via the DWARF4 std
TEST_CASE("Line table", "[dwarf]") {
	auto path = "targets/hello_kdebugger";
	kdebugger::elf elf(path);
	kdebugger::dwarf dwarf(elf);

	REQUIRE(dwarf.compile_units().size() == 1);

	auto & cu = dwarf.compile_units()[0];
	auto it = cu->lines().begin();

	REQUIRE(it->line == 2);
	REQUIRE(it->file_entry->path.filename() == "hello_kdebugger.cpp");

	++it;
	REQUIRE(it->line == 3);

	++it;
	REQUIRE(it->line == 4);

	++it;
	REQUIRE(it->end_sequence);

	++it;
	REQUIRE(it == cu->lines().end());
}

// in case - source breakpoints work and show correct demangled
// function names and address' caught from the line table
TEST_CASE("Source-level breakpoints", "[breakpoint]") {
	auto dev_null = open("/dev/null", O_WRONGLY);
	auto target = target::launch("targets/overloaded", dev_null);
	auto & proc = target->get_process();

	target->create_line_breakpoint("overloaded.cpp", 17).enable();

	proc.resume();
	proc.wait_on_signal();

	auto entry = target->line_entry_at_pc();
	REQUIRE(entry->file_entry->path.filename() == "overloaded.cpp");
	REQUIRE(entry->line == 17);

	auto & bkpt = target->create_function_breakpoint("print_type");
	bkpt.enable();

	kdebugger::breakpoint_site * lowest_bkpt = nullptr;
	bkpt.breakpoint_sites().for_each([&lowest_bkpt] (auto & site) {
		if(lowest_bkpt == nullptr || site.address().addr() < lowest_bkpt->address().addr()) {
			lowest_bkpt = &site;
		}
	});

	lowest_bkpt->disable();

	proc.resume();
	proc.wait_on_signal();

	REQUIRE(target->line_entry_at_pc()->line == 9);

	proc.resume();
	proc.wait_on_signal();

	REQUIRE(target->line_entry_at_pc()->line == 13);

	proc.resume();
	auto reason = proc.wait_on_signal();

	REQUIRE(reason.reason == kdebugger::process_state::exited);
	close(dev_null);
}

// in case - stepping at source level into functions, over signatures
// and through the program works
TEST_CASE("Source-level stepping", "[target]") {
	auto dev_null = open("/dev/null", O_WRONGLY);
	auto target = target::launch("target/step", dev_null);
	auto & proc = target->get_process();

	target->create_function_breakpoint("main").enable();
	proc.resume();
	proc.wait_on_signal();

	auto pc = proc.get_pc();
	REQUIRE(target->function_name_at_address(pc) == "main");

	target->step_over();

	auto new_pc = proc.get_pc();
	REQUIRE(new_pc != pc);
	REQUIRE(target->function_name_at_address(pc) == "main");

	target->step_in();

	pc = proc.get_pc();
	REQUIRE(target->function_name_at_address(pc) == "find_happiness");
	REQUIRE(target->get_stack().inline_height() == 2);

	target->step_in();
	new_pc = proc.get_pc();
	REQUIRE(new_pc == pc);
	REQUIRE(target->get_stack().inline_height() == 1);

	target->step_out();

	new_pc = proc.get_pc();
	REQUIRE(new_pc != pc);
	REQUIRE(target->function_name_at_address(pc) == "find_happiness");

	target->step_out();

	pc = proc.get_pc();
	REQUIRE(target->function_name_at_address(pc) == "main");
	close(dev_null);
}

// in case - check stack unwinding works
TEST_CASE("Stack unwinding", "[unwind]") {
	auto target = target::launch("targets/step");
	auto & proc = target->get_process();

	target->create_function_breakpoint("scratch_ears").enable();
	proc.resume();
	proc.wait_on_signal();
	target->step_in();
	target->step_in();

	std::vector<std::string_view> expected_names = {
		"scratch_ears",
		"pet_cat",
		"find_happiness",
		"main"
	};

	auto frames = target->get_stack().frames();
	for(auto i = 0; i < frames.size(); ++i) {
		REQUIRE(frames[i].func_die.name().value() == expected_names[i]);
	}
}

// in case - loading shared libraries works correctly
TEST_CASE("Shared library tracing works", "[dynlib]") {
	auto dev_null = open("/dev/null", O_WRONGLY);
	auto target = target::launch("targets/marshmellow", dev_null);
	auto & proc = target->get_process();

	target->create_function_breakpoint("libmarshmellow_test").enable();
	proc.resume();
	proc.wait_on_signal();
	
	REQUIRE(target->get_stack().frames().size() == 2);
	REQUIRE(target->get_stack().frames()[0].func_die.name().value() == "libmarshmellow_test");
	REQUIRE(target->get_stack().frames()[1].func_die.name().value() == "main");
	REQUIRE(target->get_pc_file_address().elf_file()->path().filename() == "libmeow.so");
	close(dev_null);
}

// in case - testing that multi threading works
TEST_CASE("Multi-Threading works", "[thread]") {
    auto dev_null = open("/dev/null", O_WRONGLY);
    auto target = target::launch("targets/multi_threaded", dev_null);
    auto & proc = target->get_process();

    target->create_function_breakpoint("say_hi").enable();

    std::set<pid_t> tids;
    stop_reason reason;

    do {
        proc.resume_all_threads();
        reason = proc.wait_on_signal();
        
        for(auto & [tid, thread] : proc.thread_states()) {
            if(thread.reason.reason == kdebugger::process_state::stopped && tid != proc.pid()) {
                tids.insert(tid);
            }
        }
    }

    while(tids.size() < 10);

    REQUIRE(tids.size() == 10);

    proc.resume_all_threads();
    reason = proc.wait_on_signal();
    REQUIRE(reason.reason == kdebugger::process_state::exited);
    close(dev_null);
}
