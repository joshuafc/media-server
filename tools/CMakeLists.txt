add_executable(puller ${CMAKE_SOURCE_DIR}/tools/puller.cpp
                      ${CMAKE_SOURCE_DIR}/src/util.cpp)
if(WITH_GLOG)
    target_link_libraries(puller ${GLOG_LIBRARY} )
endif()
target_link_libraries(puller ${BRPC_LIB} ${DYNAMIC_LIB})
install(TARGETS puller
        RUNTIME DESTINATION bin
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        )

add_executable(pusher ${CMAKE_SOURCE_DIR}/tools/pusher.cpp
                      ${CMAKE_SOURCE_DIR}/src/util.cpp
                      ${CMAKE_SOURCE_DIR}/src/frame_queue.cpp)
if(WITH_GLOG)
    target_link_libraries(pusher ${GLOG_LIBRARY} )
endif()
target_link_libraries(pusher ${BRPC_LIB} ${DYNAMIC_LIB})
install(TARGETS pusher
        RUNTIME DESTINATION bin
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        )

add_executable(rtmp_press ${CMAKE_SOURCE_DIR}/tools/rtmp_press.cpp
                          ${CMAKE_SOURCE_DIR}/src/util.cpp
                          ${CMAKE_SOURCE_DIR}/src/checksum.cpp)
if(WITH_GLOG)
    target_link_libraries(rtmp_press ${GLOG_LIBRARY} )
endif()
target_link_libraries(rtmp_press ${BRPC_LIB} ${DYNAMIC_LIB})
install(TARGETS rtmp_press
        RUNTIME DESTINATION bin
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        )

add_executable(random_test ${CMAKE_SOURCE_DIR}/tools/random_test.cpp
                           ${CMAKE_SOURCE_DIR}/src/util.cpp
                           ${CMAKE_SOURCE_DIR}/src/frame_queue.cpp)
if(WITH_GLOG)
    target_link_libraries(random_test ${GLOG_LIBRARY} )
endif()
target_link_libraries(random_test ${BRPC_LIB} ${DYNAMIC_LIB})
install(TARGETS random_test
        RUNTIME DESTINATION bin
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        )

