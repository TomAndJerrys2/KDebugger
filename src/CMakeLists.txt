# adds library files from src and namspaces
# this is a convenience for error handling later
add_library(libkdebugger libkdebugger.cpp)
add_library(kdebugger::libkdebugger ALIAS libkdebugger)

# property of where to output the libkdebugger library
# and what to call the binaries
set_target_properties(
	libkdebugger
	PROPERTIES OUTPUT_NAME kdebugger
)

# I am using C++17 for this - though I may eventually
# integrate coroutines and possibly some meta-programming
# C++20 & 23 features give. Just to show use-cases for 
# newer language versions
target_compile_features(libkdebugger PUBLIC cxx_std_17)

# Generator expression to contain public
# includes in our project tree
target_include_directories(
	libkdebugger
	
	PUBLIC
	$<INSTALL_INTERFACE:include>
	$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>	
	
	PRIVATE
	${CMAKE_SOURCE_DIR}/src/include
)

# tells cmake where each of the components for 
# kdebugger should go using GNUInstallDirs
# which has a suite of global variables for this.
include(GNUInstallDirs)
install(TARGETS libkdebugger
	EXPORT kdebugger-targets

	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
	ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}

	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
	INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# installs the public headers into our include folder
# again for the project tree.
install (
	DIRECTORY ${PROJECT_SOURCE_DIR}/include/
	DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# this specifically makes it importable by other
# cmake-using projects, ill need this when I come
# to make a disassembler and fork each repo to 
# create a grand-suite of tools.
install (
	EXPORT kdebugger-targets
	FILE kdebugger-config.cmake
	NAMESPACE kdebugger::

	DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/kdebugger
)

# installs it as a command line tool
# this is specifically for linux distros
include(GNUInstallDirs)
install(
	TARGETS libkdebugger
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
