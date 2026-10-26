add_executable(openrayman_tests
	main.cc
	binary_reader_tests.cc
	checksum_tests.cc
	format_tests.cc
	game_data_tests.cc
	level_tests.cc
)
target_link_libraries(openrayman_tests PRIVATE openrayman::lib doctest::doctest)
openrayman_warnings(openrayman_tests)

# Tests against a real installation run when OPENRAYMAN_GAME_DIR is set, and are skipped otherwise.
add_test(NAME openrayman_tests COMMAND openrayman_tests)
