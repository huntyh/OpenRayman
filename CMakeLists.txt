cmake_minimum_required(VERSION 3.24)
project(OpenRayman VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(OPENRAYMAN_BUILD_ENGINE "Build the openrayman engine executable" ON)
option(OPENRAYMAN_BUILD_TOOLS "Build the ortool command line tool" ON)
option(OPENRAYMAN_BUILD_TESTS "Build the unit tests" ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

if(MSVC)
	add_compile_options(/utf-8 /permissive- /MP)
endif()

# Warnings for our own targets only, dependencies keep their own settings.
function(openrayman_warnings target)
	if(MSVC)
		target_compile_options(${target} PRIVATE /W4)
	else()
		target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
	endif()
endfunction()

include(cmake/Dependencies.cmake)

add_subdirectory(lib)
add_subdirectory(libopenrayman)

if(OPENRAYMAN_BUILD_ENGINE)
	add_subdirectory(src)
endif()

if(OPENRAYMAN_BUILD_TOOLS)
	add_subdirectory(tools/ortool)
endif()

if(OPENRAYMAN_BUILD_TESTS)
	enable_testing()
	add_subdirectory(tests)
endif()
