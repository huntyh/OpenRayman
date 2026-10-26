# Runs ortool and compares a checksum of its output with the expected one. Only checksums are kept
# in the repository: the output is made of the game's data.
#
#   cmake -DORTOOL=<path> -DNAME=<check name> -DARGS=<ortool arguments, ; separated> -DEXPECTED=<sha256>
#         -DOUT_DIR=<folder for the output on failure> -P ortool_check.cmake
#
# "<game>" in ARGS stands for OPENRAYMAN_GAME_DIR; without it the check is skipped. The game folder is
# replaced by "<game>" in the output and line endings are normalized, so the checksum doesn't depend
# on where the game is installed or on the platform.

cmake_minimum_required(VERSION 3.25)

set(game "$ENV{OPENRAYMAN_GAME_DIR}")
if("<game>" IN_LIST ARGS)
	if(game STREQUAL "")
		message("SKIPPED: OPENRAYMAN_GAME_DIR not set")
		return()
	endif()
	list(FIND ARGS "<game>" at)
	list(REMOVE_AT ARGS ${at})
	list(INSERT ARGS ${at} "${game}")
endif()

execute_process(COMMAND "${ORTOOL}" ${ARGS} OUTPUT_VARIABLE output ERROR_VARIABLE errors RESULT_VARIABLE result)
if(NOT result EQUAL 0)
	message(FATAL_ERROR "ortool ${ARGS} failed (${result}): ${errors}")
endif()

string(REPLACE "\r\n" "\n" output "${output}")
if(NOT game STREQUAL "")
	file(TO_CMAKE_PATH "${game}" game_forward)
	string(REPLACE "${game}" "<game>" output "${output}")
	string(REPLACE "${game_forward}" "<game>" output "${output}")
endif()

string(SHA256 actual "${output}")
if(NOT actual STREQUAL EXPECTED)
	file(WRITE "${OUT_DIR}/${NAME}.txt" "${output}")
	message(FATAL_ERROR "ortool ${NAME}: output changed\n  expected ${EXPECTED}\n  actual   ${actual}\n"
		"  output saved to ${OUT_DIR}/${NAME}.txt; if the change is right, pin the new checksum in tests/CMakeLists.txt")
endif()
message("ortool ${NAME}: ${actual}")
