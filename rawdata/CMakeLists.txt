project(
  RawData VERSION 0.1 DESCRIPTION "Use E2SAR to transmit raw NSLCDAQ data"
  LANGUAGES CXX
  )

set(CMAKE_INSTALL_RPATH ${NSCLDAQ_LIB})

set(target_includes
  ${CMAKE_SOURCE_DIR}/utils
  ${NSCLDAQ_INC}
  )
set(target_libs
  ${E2SAR_LIBRARIES}
  ${Boost_LIBRARIES}
  ${GRPC_LIBRARIES}
  ${Protobuf_LIBRARIES}
  Threads::Threads
  ${NSCLDAQ_LIB}/libdaqio.so
  ${NSCLDAQ_LIB}/libdataformat.so
  ${NSCLDAQ_LIB}/liburl.so
  FribEjfatUtils
  )

set(send_exec send_rawdata)
set(send_src send.cpp)
add_executable(${send_exec} ${send_src})
target_include_directories(${send_exec} PRIVATE ${target_includes})
target_link_libraries(${send_exec} ${target_libs})
set_target_properties(${send_exec} PROPERTIES OUTPUT_NAME send)

set(recv_exec recv_rawdata)
set(recv_src recv.cpp)
add_executable(${recv_exec} ${recv_src})
target_include_directories(${recv_exec} PRIVATE ${target_includes})
target_link_libraries(${recv_exec} ${target_libs})
set_target_properties(${recv_exec} PROPERTIES OUTPUT_NAME recv)

install(TARGETS ${send_exec} ${recv_exec} DESTINATION bin/rawdata)
