# Conan third-party libraries
include(${CMAKE_SOURCE_DIR}/lib/cmake-conan/conan.cmake)
include(${CMAKE_SOURCE_DIR}/lib/cmake-conan/conan-omp.cmake)

if(BUILD_SERVER)
if(BUILD_LEGACY_COMPONENTS)
# RakNet
# ------
add_subdirectory(RakNet)
set_property(TARGET raknet PROPERTY FOLDER "lib")
target_compile_options(raknet
	PRIVATE
		"-Wno-logical-op-parentheses"
		"-Wno-bitwise-op-parentheses"
		"-Wno-#pragma-messages"
)
endif()

# ttmath
# ------
add_subdirectory(ttmath)
GroupSourcesByFolder(ttmath)
set_property(TARGET ttmath PROPERTY FOLDER "lib")

# Lua runtime
# -----------
if(BUILD_LUA_COMPONENT)
	add_subdirectory(lua)
	GroupSourcesByFolder(lua-lib)
	set_property(TARGET lua-lib PROPERTY FOLDER "lib")
endif()

# JSON
# ----
conan_omp_add_lib(nlohmann_json 3.9.1)

# OpenSSL
# -------
	if(SHARED_OPENSSL)
		message("Using shared OpenSSL")
	conan_omp_add_lib_opt(openssl 3.0.13 "openssl:shared=True")
else()
		message("Using static OpenSSL")
	conan_omp_add_lib(openssl 3.0.13)
endif()

# ghc-filesystem
# ----
conan_omp_add_lib(ghc-filesystem 1.5.12)

if(BUILD_SQLITE_COMPONENT)
	# SQLite 3
	# --------
	conan_omp_add_lib_opt(sqlite3 3.36.0 "sqlite3:enable_math_functions=False")
endif()

if(BUILD_UNICODE_COMPONENT)
	# ICU
	# ---
	conan_omp_add_lib(icu 74.2)

	if(WIN32)
		target_link_options(CONAN_PKG::icu INTERFACE "/SAFESEH:NO")
	endif()
endif()
endif()

if(BUILD_ABI_CHECK_TOOL)
	conan_omp_add_lib(libelfin 0.3)
endif()

# cxxopts
# ----
conan_omp_add_lib(cxxopts 2.2.1)
