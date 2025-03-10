set(CMAKE_INSTALL_RPATH ${NSCLDAQ_LIB})

set(common_includes
  ${CMAKE_SOURCE_DIR}/utils
  ${NSCLDAQ_INC}
  )
set(common_libs
  ${PROJECT_LIBS}
  ${NSCLDAQ_LIB}/libdaqio.so
  ${NSCLDAQ_LIB}/libdataformat.so
  ${NSCLDAQ_LIB}/liburl.so
  FribEjfatUtils
  )

set(send_exec send_evtbuild)
set(send_src send.cpp)
add_executable(${send_exec} ${send_src})
target_include_directories(${send_exec} PRIVATE ${common_includes})
target_link_libraries(${send_exec} ${common_libs})
set_target_properties(${send_exec} PROPERTIES OUTPUT_NAME send)

set(recv_exec recv_evtbuild)
set(recv_src recv.cpp)
add_executable(${recv_exec} ${recv_src})
target_include_directories(${recv_exec} PRIVATE ${common_includes})
target_link_libraries(${recv_exec} ${common_libs})
set_target_properties(${recv_exec} PROPERTIES OUTPUT_NAME recv)

install(TARGETS ${send_exec} ${recv_exec} DESTINATION bin/evtbuild)
