project(
  EjfatIO VERSION 0.1 DESCRIPTION "I/O classes for FRIB-EJFAT processing"
  LANGUAGES CXX
  )

set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH ${UFMT_LIB} ${NSCLDAQ_LIB})

set(src
  CDataSink.cpp CFileDataSink.cpp CRingDataSink.cpp
  DataSource.cpp FdDataSource.cpp StreamDataSource.cpp
  )
set(headers
  CDataSink.h CFileDataSink.h CRingDataSink.h
  DataSource.h FdDataSource.h StreamDataSource.h
  )
set(lib EjfatIO)

add_library(${lib} STATIC ${src})

target_include_directories(${lib} PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${UFMT_INC}
  ${NSCLDAQ_INC}
  )
target_link_libraries(
  ${lib}
  ${E2SAR_LIBRARIES}
  ${Boost_LIBRARIES}
  ${GRPC_LIBRARIES}
  ${Protobuf_LIBRARIES}
  Threads::Threads
  ${NSCLDAQ_LIB}/libDataFlow.so
  ${NSCLDAQ_LIB}/libException.so
  )

set_target_properties(${lib} PROPERTIES EXPORT_NAME ${lib})

install(TARGETS ${lib} DESTINATION lib)
install(FILES ${headers} DESTINATION include)
