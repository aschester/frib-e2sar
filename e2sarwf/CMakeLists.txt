set(CMAKE_INSTALL_RPATH ${NSCLDAQ_LIB} ${CMAKE_INSTALL_PREFIX}/lib)

##
# e2sarwf
#

set(wf_includes
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

set(wf_libs
  ${PROJECT_LIBS}
  ${UFMT_LIBS}
  ${Exception}
  ${url}
  ${DataFlow}
  )

set(wf_exec e2sarwf)
set(wf_src
  e2sarwf.cpp Sender.cpp Receiver.cpp FribE2sarUtils.cpp BufferPool.cpp DataSource.cpp RingDataSource.cpp FdDataSource.cpp StreamDataSource.cpp DataSink.cpp FileDataSink.cpp RingDataSink.cpp
  )
set(wf_headers
  Sender.h Receiver.h FribE2sarUtils.h BufferPool.h DataSource.h FdDataSource.h RingDataSource.h StreamDataSource.h DataSink.h FileDataSink.h RingDataSink.h
  )
add_executable(${wf_exec} ${wf_src})
target_include_directories(${wf_exec} PRIVATE ${wf_includes})
target_link_libraries(${wf_exec} ${wf_libs})
install(FILES ${wf_headers} DESTINATION include)
install(TARGETS ${wf_exec} DESTINATION bin)

##
# evbfilter
#

set(filter_includes
  ${NSCLDAQ_INC}
  ${UFMT_INC}
  )

# Ensure we have the correct NSCLDAQ libs:
find_library(daqshm NAMES libdaqshm.so PATHS ${NSCLDAQ_LIB})
if(NOT daqshm)
  message(FATAL_ERROR "Cannot find libdaqshm.so in ${NSCLDAQ_LIB}")
endif()
find_library(dataformat NAMES libdataformat.so PATHS ${NSCLDAQ_LIB})
if(NOT dataformat)
  message(FATAL_ERROR "Cannot find libdataformat.so in ${NSCLDAQ_LIB}")
endif()

set(filter_libs
  ${Boost_LIBRARIES}
  ${daqshm}
  ${dataformat}
  ${UFMT_LIBS}
  )

set(filter_exec evbfilter)
set(filter_src evbfilter.cpp)
add_executable(${filter_exec} ${filter_src})
target_include_directories(${filter_exec} PRIVATE ${filter_includes})
target_link_libraries(${filter_exec} ${filter_libs})
install(TARGETS ${filter_exec} DESTINATION bin)
