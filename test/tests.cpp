#include <catch2/catch_test_macros.hpp>
#include <libkdebugger/process.hpp>

using namespace kdebugger;

namespace {
	
	bool process_exists(const pid_t pid);
}

TEST_CASE("process::launch success", "[process]") {
	auto proc = process::launch("yes");
	REQUIRE(process_exists(proc->pid));
}
