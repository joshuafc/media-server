protobuf_generate_cpp(PROTO_SRC PROTO_HEADER
                      ${CMAKE_SOURCE_DIR}/src/media_server.proto)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

add_executable(media_server ${SOURCES} ${PROTO_SRC})
if(WITH_GLOG)
    target_link_libraries(media_server ${GLOG_LIBRARY} )
endif()
target_link_libraries(media_server stdc++fs ${BRPC_LIB} ${DYNAMIC_LIB} )

install(TARGETS media_server
        RUNTIME DESTINATION bin
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        )

install(DIRECTORY ${CMAKE_SOURCE_DIR}/web_root DESTINATION bin)

file(COPY ${CMAKE_SOURCE_DIR}/web_root/ DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/output/bin/web_root/)

