cmake_minimum_required(VERSION 3.25)

set(game "$ENV{OPENRAYMAN_GAME_DIR}")
if(game STREQUAL "")
	message("SKIPPED: OPENRAYMAN_GAME_DIR not set")
	return()
endif()

set(approach "48.7,52.4,-75,0@30;48.3,43.2,-78.86,0@60;47.68,23.98,-80.58,0@90")

function(check_trail name ticks placements pickups stop_tick final_event final_count)
	execute_process(COMMAND "${CMAKE_COMMAND}" -E env "ORTOOL_START=new" "ORTOOL_SOUNDS=1"
		"ORTOOL_PLACE=${placements}" "${ORTOOL}" run "${game}" Bast_20 ${ticks} 56
		OUTPUT_VARIABLE trace ERROR_VARIABLE errors RESULT_VARIABLE result)
	if(NOT result EQUAL 0)
		message(FATAL_ERROR "Bast_20 timed Lums (${name}): ortool failed (${result}): ${errors}")
	endif()
	if(NOT trace MATCHES "tick +90: sound 0x008C0000 from node 3 " OR
	   NOT trace MATCHES "tick +${stop_tick}: sound 0x008C0001 from node 3 " OR
	   NOT trace MATCHES "node +55: behaviour 0 -> 1, state 0 [(]animation 276[)] -> 2 [(]animation 278[)], removed" OR
	   NOT trace MATCHES "node 56 reached 0 missing handlers:")
		message(FATAL_ERROR "Bast_20 timed Lums (${name}): expected animation change, clock events and controller execution")
	endif()
	string(REGEX MATCHALL "sound 0x00320000 from node (55|57|58|59|60) " pickup_events "${trace}")
	string(REGEX MATCHALL "tick +${stop_tick}: sound 0x${final_event} from node [0-9]+ " final_events "${trace}")
	list(LENGTH pickup_events actual_pickups)
	list(LENGTH final_events actual_final_count)
	if(NOT actual_pickups EQUAL pickups OR NOT actual_final_count EQUAL final_count)
		message(FATAL_ERROR "Bast_20 timed Lums (${name}): ${actual_pickups} pickups and ${actual_final_count} final events, expected ${pickups} and ${final_count}")
	endif()
endfunction()

check_trail(completion 240
	"${approach};53.2,15.4,-79.2,0@120;49.2,7.7,-80.6,0@150;49.5,-2.1,-78.6,0@180;49.2,-20.5,-78,0@210"
	5 211 003C0000 1)
check_trail(timeout 580 "${approach}" 1 570 00930000 4)
