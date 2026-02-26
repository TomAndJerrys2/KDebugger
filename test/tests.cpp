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
}

namespace {
	
	char get_process_status(const pid_t pid) {
		std::ifstream stat("/proc/" + std::to_string(pid) + "/stat");
		std::string data;
		std::getline(stat, data);

		auto index_of_last_parenthesis = data.rfind(')');
		auto index_of_status_indicator = index_of_last_parenthesis + 2;
		return data[index_of_status_indicator];
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
}


