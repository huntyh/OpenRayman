# The openrayman engine. GPLv3.

add_executable(openrayman WIN32
	main.cc
	audio/music_player.cc
	renderer/gl.cc
)
target_include_directories(openrayman PRIVATE .)
target_compile_definitions(openrayman PRIVATE OPENRAYMAN_VERSION="${PROJECT_VERSION}")
target_link_libraries(openrayman PRIVATE openrayman::lib SDL3::SDL3 glad glm::glm)
openrayman_warnings(openrayman)

if(WIN32)
	target_sources(openrayman PRIVATE "${PROJECT_SOURCE_DIR}/win32/app.rc")
endif()
