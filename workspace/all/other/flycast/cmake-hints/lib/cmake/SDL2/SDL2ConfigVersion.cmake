# Companion version file for SDL2Config.cmake (see that file for why this
# hand-written pair exists). Queries the sysroot's sdl2.pc for the real
# version so this stays correct across toolchain images/updates instead of
# hardcoding a number.
find_package(PkgConfig QUIET)
if(PKG_CONFIG_EXECUTABLE)
	pkg_check_modules(_SDL2_VER QUIET sdl2)
endif()

if(_SDL2_VER_VERSION)
	set(PACKAGE_VERSION "${_SDL2_VER_VERSION}")
else()
	set(PACKAGE_VERSION "2.0.9")
endif()

if(PACKAGE_VERSION VERSION_LESS PACKAGE_FIND_VERSION)
	set(PACKAGE_VERSION_COMPATIBLE FALSE)
else()
	set(PACKAGE_VERSION_COMPATIBLE TRUE)
	if(PACKAGE_VERSION VERSION_EQUAL PACKAGE_FIND_VERSION)
		set(PACKAGE_VERSION_EXACT TRUE)
	endif()
endif()
