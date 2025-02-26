cmake_minimum_required(VERSION 3.18)
project(
  CheckDefaults VERSION 0.1 DESCRIPTION "E2SAR check defaults" LANGUAGES CXX
  )
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_executable(checkdefaults checkdefaults.cpp)
target_link_libraries(
  checkdefaults
  ${E2SAR_LIBRARIES}
  ${Boost_LIBRARIES}
  ${GRPC_LIBRARIES}
  ${Protobuf_LIBRARIES}
  Threads::Threads
  )

install(TARGETS checkdefaults DESTINATION bin/checkdefaults)
