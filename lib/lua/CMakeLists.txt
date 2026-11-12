cmake_minimum_required(VERSION 3.19)
project(lua-lib C)

file(GLOB LUA_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.c")
list(REMOVE_ITEM LUA_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/lua.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/luac.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/onelua.c"
)

add_library(lua-lib STATIC ${LUA_SOURCES})

target_include_directories(lua-lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(MSVC)
    target_compile_definitions(lua-lib PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
