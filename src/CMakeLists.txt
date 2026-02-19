add_library(libkdebugger kdebugger.cpp)
add_library(kdebugger::kdebugger ALIAS kdebugger)

set_target_properties(
	kdebugger
	PROPERTIES OUTPUT_NAME kdebug
)

target_compile_features(kdebugger PUBLIC cxx_std_17)

target_include_directories(
	kdebugger
	PRIVATE include
	PUBLIC ${CMAKE_SOURCE_DIR}/include
)
