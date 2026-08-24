add_executable(ufa_example main.c)
target_link_libraries(ufa_example PRIVATE ufa)
target_compile_features(ufa_example PRIVATE c_std_11)

if (EMSCRIPTEN)
	target_compile_options(ufa_example PRIVATE "--use-port=sdl3")
	target_link_options(ufa_example PRIVATE "--use-port=sdl3")
	set_target_properties(ufa_example PROPERTIES SUFFIX ".html")
endif ()
