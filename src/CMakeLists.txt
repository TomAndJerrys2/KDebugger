add_library(libkdebugger libkdebugger.cpp)
add_library(kdebugger::libkdebugger ALIAS libkdebugger)

set_target_properties(
	libkdebugger
	PROPERTIES OUTPUT_NAME kdebug
)

target_compile_features(libkdebugger PUBLIC cxx_std_17)

target_include_directories(
	libkdebugger
	
	PUBLIC
	$<INSTALL_INTERFACE:include>
	$<BUILD_INTERFACE:$CMAKE_SOURCE_DIR}/include>	
	
	PRIVATE
	${CMAKE_SOURCE_DIR}/src/include
)
