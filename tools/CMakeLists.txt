add_executable(kdebugger kdebugger.cpp)

# Link libraries via VCPKG
target_link_libraries(kdebugger 
	PRIVATE kdebugger::libkdebugger 
	PkgConfig::libedit
)

target_compile_features(kdebugger PRIVATE cxx_std_23)
