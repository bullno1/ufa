add_library(ufa STATIC ufa.c)
target_include_directories(ufa PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../include")
target_compile_features(ufa PRIVATE c_std_11)

if (EMSCRIPTEN)
	# The JS implementation can only be linked into the final executable,
	# hence INTERFACE
	set(UFA_JS_LIB "${CMAKE_CURRENT_SOURCE_DIR}/ufa.js")
	target_link_options(ufa INTERFACE "SHELL:--js-library ${UFA_JS_LIB}")
	set_property(TARGET ufa APPEND PROPERTY INTERFACE_LINK_DEPENDS "${UFA_JS_LIB}")
else ()
	if (NOT TARGET SDL3::SDL3)
		find_package(SDL3 REQUIRED CONFIG)
	endif ()
	target_link_libraries(ufa PUBLIC SDL3::SDL3)
endif ()
