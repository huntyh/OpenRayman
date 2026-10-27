add_executable(openrayman_tests
	main.cc
	binary_reader_tests.cc
	checksum_tests.cc
	format_tests.cc
	game_data_tests.cc
	animation_tests.cc
	level_tests.cc
)
target_link_libraries(openrayman_tests PRIVATE openrayman::lib openrayman::game doctest::doctest)
openrayman_warnings(openrayman_tests)

# Tests against a real installation run when OPENRAYMAN_GAME_DIR is set, and are skipped otherwise.
add_test(NAME openrayman_tests COMMAND openrayman_tests)

# ortool's output, checked by checksum (see ortool_check.cmake). After a deliberate change, the failing
# check saves the new output under build/<preset>/tests/ortool_output and prints the new checksum.
function(ortool_check name expected)
	add_test(NAME ortool_${name}
		COMMAND ${CMAKE_COMMAND} -DORTOOL=$<TARGET_FILE:ortool> -DNAME=${name} "-DARGS=${ARGN}" -DEXPECTED=${expected}
			-DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/ortool_output -P ${CMAKE_CURRENT_SOURCE_DIR}/ortool_check.cmake)
	set_tests_properties(ortool_${name} PROPERTIES SKIP_REGULAR_EXPRESSION "SKIPPED:")
endfunction()

ortool_check(help
	9208b7dc0c81b361a08ec2262dc9c890e8f935c4744b432dc76a2f4e2fe80adc
	help)
ortool_check(level_learn_10
	743b90c253503738472e50d824739146264cad8afae6aca4ed26cfb26a28f7f8
	level <game> Learn_10)
ortool_check(scene_learn_10
	3fe286e1d8312574c6873d4cfbb6cac2a79abcbd65485434cba37ce83effa1c9
	scene <game> Learn_10)
ortool_check(lights_learn_10
	e3271af66002ab034b1b2309ae2d86a222518117985cd9ecc23d884c4a2e2eb2
	lights <game> Learn_10)
ortool_check(motion_learn_10
	a6320d837212e5b1495b5bd30e4ed9f0f4830ec949d27fd68a03a102e43f2475
	motion <game> Learn_10)
ortool_check(states_learn_10_cage
	23004b4c93ad4163b9b5be4ec49f6e0b5040dd564431d57eee8150ad473f20bc
	states <game> Learn_10 53)
ortool_check(brain_learn_10
	864b40b3a3f85e05898513aebc4b6bdc9f7df1b24038aab02c935e2e049dc6a1
	brain <game> Learn_10)
ortool_check(brain_learn_10_rayman
	badc103a48990cbb2b291844d0939a5804600a63a2e4037b3ebabcb053d3902e
	brain <game> Learn_10 84)
ortool_check(brain_all
	2bb89eb2af4a3ac3bb2fab563becd3401015ee4897f491b56db81f52a18ae82f
	brain <game> all)
