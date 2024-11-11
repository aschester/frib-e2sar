##
# An example CMakeLists.txt for E2SAR
# ASC 10/24/24
#

project(
  FromINI VERSION 0.1 DESCRIPTION "Configure E2SAR using .ini files"
  LANGUAGES CXX
  )

set(send_exec send_fromini)
set(send_src send.cpp)
add_executable(${send_exec} ${send_src})
target_link_libraries(
  ${send_exec}
  ${E2SAR_LIBRARIES}
  ${Boost_LIBRARIES}
  ${GRPC_LIBRARIES}
  ${Protobuf_LIBRARIES}
  Threads::Threads
  )

set(recv_exec recv_fromini)
set(recv_src recv.cpp)
add_executable(${recv_exec} ${recv_src})
target_link_libraries(
  ${recv_exec}
  ${E2SAR_LIBRARIES}
  ${Boost_LIBRARIES}
  ${GRPC_LIBRARIES}
  ${Protobuf_LIBRARIES}
  Threads::Threads
  )

install(TARGETS ${send_exec} ${recv_exec} DESTINATION bin)
