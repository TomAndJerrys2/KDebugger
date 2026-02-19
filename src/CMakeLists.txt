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

include(GNUInstallDirs)
install(TARGETS libkdebugger
	EXPORT kdebugger-targets

	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
	ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}

	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
	INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install (
	DIRECTORY ${PROJECT_SOURCE_DIR}/include/
	DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install (
	EXPORT kdebugger-targets
	FILE kdebugger-config.cmake
	NAMESPACE kdebugger::

	DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/kdebugger
)
