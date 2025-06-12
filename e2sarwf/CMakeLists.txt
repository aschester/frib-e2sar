set(CMAKE_INSTALL_RPATH ${NSCLDAQ_LIB} ${CMAKE_INSTALL_PREFIX}/lib)

###########################################################################
# Common includes/libs:
##

set(common_includes
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${UFMT_INC}
  ${NSCLDAQ_INC}
  )
set(common_libs
  ${PROJECT_LIBS}
  ${UFMT_LIBS}
  ${NSCLDAQ_LIB}/libException.so
  ${NSCLDAQ_LIB}/liburl.so
  ${NSCLDAQ_LIB}/libDataFlow.so
  )

##
# e2sarwf
#

set(exec e2sarwf)
set(src
  e2sarwf.cpp Sender.cpp Receiver.cpp FribE2sarUtils.cpp DataSource.cpp RingDataSource.cpp FdDataSource.cpp StreamDataSource.cpp DataSink.cpp FileDataSink.cpp RingDataSink.cpp
  )
set(headers
  Sender.h Receiver.h FribE2sarUtils.h DataSource.h FdDataSource.h RingDataSource.h StreamDataSource.h DataSink.h FileDataSink.h RingDataSink.h
  )
add_executable(${exec} ${src})
target_include_directories(${exec} PRIVATE ${common_includes})
target_link_libraries(${exec} ${common_libs})
install(FILES ${headers} DESTINATION include)
install(TARGETS ${exec} DESTINATION bin)

##
# evbfilter
#

set(filter_includes
  ${NSCLDAQ_INC}
  ${UFMT_INC}

  )
set(filter_libs
  ${NSCLDAQ_LIB}/libdaqshm.so
  ${NSCLDAQ_LIB}/libEventBuilderClient.so
  ${UFMT_LIBS}
  )

set(filter_exec evbfilter)
set(filter_src evbfilter.cpp )
#set(filter_headers )
add_executable(${filter_exec} ${filter_src})
target_include_directories(${filter_exec} PRIVATE ${filter_includes})
target_link_libraries(${filter_exec} ${filter_libs})
#install(FILES ${filter_headers} DESTINATION include)
install(TARGETS ${filter_exec} DESTINATION bin)
