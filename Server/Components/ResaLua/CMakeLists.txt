get_filename_component(ProjectId ${CMAKE_CURRENT_SOURCE_DIR} NAME)
add_server_component(${ProjectId})

target_link_libraries(${PROJECT_NAME} PRIVATE
    lua-lib
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/lib/lua
    ${CMAKE_SOURCE_DIR}/CAPI/include
)
