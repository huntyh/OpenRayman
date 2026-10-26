# Vendored single-file libraries.

add_library(glad STATIC glad/src/gl.c)
target_include_directories(glad PUBLIC glad/include)

add_library(stb INTERFACE)
target_include_directories(stb INTERFACE stb)
if(MSVC)
	target_compile_definitions(stb INTERFACE _CRT_SECURE_NO_WARNINGS)
endif()
