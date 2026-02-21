add_executable(kdebugger kdebugger.cpp)

# Link libraries via VCPKG
target_link_libraries(kdebugger 
	PRIVATE kdebugger::libkdebugger 
	PkgConfig::libedit
)
