cmake_minimum_required(VERSION 3.18)
project(
  CheckDefaults VERSION 0.1 DESCRIPTION "E2SAR check defaults" LANGUAGES CXX
  )
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(exec checkdefaults)
set(src checkdefaults.cpp)

add_executable(${exec} ${src})
target_link_libraries(
  ${exec}
  ${E2SAR_LIBRARIES}
  ${Boost_LIBRARIES}
  ${GRPC_LIBRARIES}
  ${Protobuf_LIBRARIES}
  Threads::Threads
  )

install(TARGETS ${exec} DESTINATION bin/checkdefaults)
