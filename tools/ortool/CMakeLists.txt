add_executable(ortool main.cc)
target_link_libraries(ortool PRIVATE openrayman::lib openrayman::game stb)
openrayman_warnings(ortool)
