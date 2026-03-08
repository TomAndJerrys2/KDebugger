// Project specific / private headers
#include <libkdebugger/syscalls.hpp>
#include <libkdebugger/error.hpp>

// General include paths
#include <unordered_map>

namespace {
	const std::unordered_map<std::string_view, int> g_syscall_name_map = {
		#define DEFINE_SYSCALL(name, id) {#name, id},
		#include "include/syscalls.inc"
		#undef DEFINE_SYSCALL 
	}
}

std::string_view kdebugger::syscall_id_to_name(int id) {
	switch(id) {
		#define DEFINE_SYSCALL(name, id) case id:
			return #name;
		#include "include/syscalls.inc"
		#undef DEFINE_SYSCALL

		default:
			kdebugger::error::send("No syscall found with that id\n");
	}
}

int kdebugger::syscall_name_to_id(std::string_view name) {
	if(g_syscall_name_map.count(name) != 1)
		kdebugger::error::send("No such syscall");

	return g_syscall_name_map.at(name);
}
