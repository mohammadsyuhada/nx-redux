# Hand-written SDL2 CMake package-config file.
#
# Why this exists: flycast/CMakeLists.txt calls plain `find_package(SDL2 2.0.9)`
# (module-then-config search, no MODULE/CONFIG keyword). The toolchain sysroot
# ships a host SDL2 via pkg-config (sdl2.pc) but no SDL2Config.cmake, and stock
# CMake ships no bundled FindSDL2.cmake module (only FindSDL.cmake for SDL 1.x).
# So the plain find_package(SDL2 2.0.9) call can never succeed on its own, and
# -DUSE_HOST_SDL=ON silently falls back to vendoring core/deps/SDL as a static
# lib (SDL2_FOUND stays false -> `if(NOT SDL2_FOUND) add_subdirectory(core/deps/SDL)`).
# This file is discovered via CMAKE_PREFIX_PATH (set in toolchain-aarch64.cmake)
# and makes the host SDL2 discoverable in CONFIG mode instead. See README
# "Build" section for the full story.
find_package(PkgConfig REQUIRED)
pkg_check_modules(PC_SDL2 REQUIRED sdl2)

if(NOT TARGET SDL2::SDL2)
	add_library(SDL2::SDL2 SHARED IMPORTED)
	set_target_properties(SDL2::SDL2 PROPERTIES
		INTERFACE_INCLUDE_DIRECTORIES "${PC_SDL2_INCLUDE_DIRS}"
		IMPORTED_LOCATION "${PC_SDL2_LIBDIR}/libSDL2.so"
	)
endif()

set(SDL2_INCLUDE_DIRS ${PC_SDL2_INCLUDE_DIRS})
set(SDL2_LIBRARIES SDL2::SDL2)
set(SDL2_VERSION ${PC_SDL2_VERSION})
set(SDL2_FOUND TRUE)
