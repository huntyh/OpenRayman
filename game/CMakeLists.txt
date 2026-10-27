# The game's logic: live memory and the script interpreter. GPLv3, no dependencies beyond libopenrayman,
# so the engine, ortool and the tests can all run the game's scripts.

add_library(openrayman_game STATIC
	src/animation_player.cc
	src/handlers.cc
	src/interpreter.cc
	src/live_memory.cc
)
add_library(openrayman::game ALIAS openrayman_game)
target_include_directories(openrayman_game PUBLIC include)
target_link_libraries(openrayman_game PUBLIC openrayman::lib)
openrayman_warnings(openrayman_game)
