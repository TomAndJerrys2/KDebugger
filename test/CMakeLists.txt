add_executable(tests tests.cpp)

target_link_libraries(
	tests PRIVATE

	kdebugger::libkdebugger
	Catch2::Catch2WithMain
)
