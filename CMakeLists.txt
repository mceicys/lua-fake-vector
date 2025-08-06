cmake_minimum_required(VERSION 3.22)
project(luafakevector VERSION 0.1)

set(LUA_DIR "${CMAKE_CURRENT_LIST_DIR}" CACHE PATH "Where Lua's headers and import library are.")
set(LUA_VERSION "5.4" CACHE STRING "Lua's major.minor version.")
set(INSTALL_CMOD_DIR "lib/lua/${LUA_VERSION}" CACHE PATH "Where to install Lua C modules.")

set(HEADERS_CORE lfv.h lfvreader.h)
set(HEADERS_WITH_LUA lfvlua.h)

if(MSVC)
	add_compile_options(/W4)
else()
	add_compile_options(-Wall -Wextra)
endif()

if(WIN32)
	message(CHECK_START "Looking for Lua import library")
	string(REPLACE "." "" LUA_VERSION_SUFFIX ${LUA_VERSION})
	
	find_library(
		LUA_IMPLIB
		NAMES lua.lib lua${LUA_VERSION_SUFFIX}.lib
		PATHS
			"${LUA_DIR}"
			"${LUA_DIR}/lib"
	)
	
	if(LUA_IMPLIB)
		message(CHECK_PASS "Found")
	else()
		message(CHECK_FAIL "Not found")
		message(WARNING "lfv will fail to build on Windows without the lua.lib import library. Make sure LUA_DIR (${LUA_DIR}) and LUA_VERSION (${LUA_VERSION}) are correct.")
	endif()
endif()

# Dynamic library (to be loaded by Lua)
add_library(lfv SHARED lfv.c lfvlua.c ${HEADERS_CORE} ${HEADERS_WITH_LUA} lfv.def)
set_target_properties(lfv PROPERTIES PREFIX "")

if(LUA_DIR)
	# Add preferred directories of Lua headers
	target_include_directories(lfv BEFORE PRIVATE "${LUA_DIR}" "${LUA_DIR}/include" "${LUA_DIR}/src")
endif()

if(LUA_IMPLIB)
	# Link Lua's import library to lfv
	add_library(luaimport SHARED IMPORTED)
	set_property(TARGET luaimport PROPERTY IMPORTED_IMPLIB "${LUA_IMPLIB}")
	target_link_libraries(lfv PRIVATE luaimport)
endif()

# Utility executable
add_executable(lfvutil lfvutil.c lfv.c)

# Install
install(TARGETS lfv DESTINATION ${INSTALL_CMOD_DIR})
install(TARGETS lfvutil DESTINATION bin)
