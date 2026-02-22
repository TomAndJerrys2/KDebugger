#include <sys/types.h>
#include <signal.h>

#include <catch2/catch_test_macros.hpp>
#include <libkdebugger/process.hpp>



using namespace kdebugger;

namespace {
	
	bool process_exists(const pid_t pid) {
		auto ret = kill(pid, 0);
		return (ret != -1 && errno != ESRCH);
	}
}

TEST_CASE("process::launch success", "[process]") {
	auto proc = process::launch("yes");
	REQUIRE(process_exists(proc->pid));
}

TEST_CASE("process::launch no such program", "[process]") {
	REQUIRE_THROW_AS(process::launch("example_program"), error);
}
