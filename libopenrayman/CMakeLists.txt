# libopenrayman: readers for Rayman 2 data formats. LGPLv3, no dependencies beyond the standard library.

add_library(libopenrayman STATIC
	src/actor.cc
	src/animation.cc
	src/apm_audio.cc
	src/cnt_archive.cc
	src/dsb_script.cc
	src/file.cc
	src/game_data.cc
	src/geometry.cc
	src/gf_texture.cc
	src/level_header.cc
	src/memory_image.cc
	src/pointer_stream.cc
	src/relocation.cc
	src/scene.cc
	src/sna_file.cc
	src/texture_table.cc
)
add_library(openrayman::lib ALIAS libopenrayman)
target_include_directories(libopenrayman PUBLIC include)
set_target_properties(libopenrayman PROPERTIES OUTPUT_NAME openrayman)
openrayman_warnings(libopenrayman)
