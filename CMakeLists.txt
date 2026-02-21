cmake_minimum_required(VERSION 3.19)

project("KDebugger" LANGUAGES CXX)

find_package(PkgConfig REQUIRED)
pkg_check_modules(libedit REQUIRED IMPORTED_TARGET libedit)

include(CTest)

add_subdirectory("src")
add_subdirectory("tools")

if(BUILD_TESTING)
	# Using VCPKGs port of Catch2 for automated
	# testing an easier use of features like assert
	find_package(Catch2 CONFIG REQUIRED)
	add_subdirectory("test")
endif()
