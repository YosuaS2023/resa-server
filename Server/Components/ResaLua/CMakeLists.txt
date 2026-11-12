get_filename_component(ProjectId ${CMAKE_CURRENT_SOURCE_DIR} NAME)
add_server_component(${ProjectId})

target_link_libraries(${ProjectId} PRIVATE
    lua-lib
    CONAN_PKG::ghc-filesystem
)

target_include_directories(${ProjectId} PRIVATE
    ${CMAKE_SOURCE_DIR}/lib/lua
    ${CMAKE_SOURCE_DIR}/CAPI/include
)
