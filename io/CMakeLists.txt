project(
  EjfatIO VERSION 0.1 DESCRIPTION "I/O classes for FRIB-EJFAT processing"
  LANGUAGES CXX
  )

set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH ${UFMT_LIB} ${NSCLDAQ_LIB})

set(io_src
  CDataSink.cpp CFileDataSink.cpp
  DataSource.cpp FdDataSource.cpp StreamDataSource.cpp
  )
set(io_headers
  CDataSink.h CFileDataSink.h
  DataSource.h FdDataSource.h StreamDataSource.h
  )

add_library(EjfatIO SHARED ${io_src})

target_include_directories(EjfatIO PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${UFMT_INC}
  ${NSCLDAQ_INC}
  )
target_link_libraries(
  EjfatIO
  ${E2SAR_LIBRARIES}
  ${Boost_LIBRARIES}
  ${GRPC_LIBRARIES}
  ${Protobuf_LIBRARIES}
  Threads::Threads
  )

set_target_properties(EjfatIO PROPERTIES EXPORT_NAME EjfatIO)

install(TARGETS EjfatIO DESTINATION lib)
install(FILES ${io_headers} DESTINATION include)
