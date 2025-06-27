set(CMAKE_INSTALL_RPATH ${NSCLDAQ_LIB} ${CMAKE_INSTALL_PREFIX}/lib)

set(includes
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${UFMT_INC}
  ${NSCLDAQ_INC}
  )

# Ensure we have the correct NSCLDAQ libs:
find_library(Exception NAMES libException.so PATHS ${NSCLDAQ_LIB})
if(NOT Exception)
  message(FATAL_ERROR "Cannot find libException.so in ${NSCLDAQ_LIB}")
endif()
find_library(url NAMES liburl.so PATHS ${NSCLDAQ_LIB})
if(NOT url)
  message(FATAL_ERROR "Cannot find liburl.so in ${NSCLDAQ_LIB}")
endif()
find_library(DataFlow NAMES libDataFlow.so PATHS ${NSCLDAQ_LIB})
if(NOT DataFlow)
  message(FATAL_ERROR "Cannot find libDataFlow.so in ${NSCLDAQ_LIB}")
endif()

set(libs
  ${PROJECT_LIBS}
  ${UFMT_LIBS}
  ${Exception}
  ${url}
  ${DataFlow}
  )

set(exec e2sarwf)
set(src
  e2sarwf.cpp Sender.cpp Receiver.cpp FribE2sarUtils.cpp BufferPool.cpp DataSource.cpp RingDataSource.cpp FdDataSource.cpp StreamDataSource.cpp BufferedSink.cpp DataSink.cpp FileDataSink.cpp RingDataSink.cpp
  )
set(headers
  Sender.h Receiver.h FribE2sarUtils.h BufferPool.h DataSource.h FdDataSource.h RingDataSource.h StreamDataSource.h BufferedSink.h DataSink.h FileDataSink.h RingDataSink.h
  )
add_executable(${exec} ${src})
target_include_directories(${exec} PRIVATE ${includes})
target_link_libraries(${exec} ${libs})
install(FILES ${headers} DESTINATION include)
install(TARGETS ${exec} DESTINATION bin)
