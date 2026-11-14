get_filename_component(ProjectId ${CMAKE_CURRENT_SOURCE_DIR} NAME)
add_server_component(${ProjectId})

target_link_libraries(${ProjectId} PRIVATE
    CONAN_PKG::mariadb-connector-c
)

if(WIN32)
    target_link_libraries(${ProjectId} PRIVATE crypt32 bcrypt secur32)
elseif(UNIX AND NOT APPLE)
    target_link_libraries(${ProjectId} PRIVATE atomic)
endif()
