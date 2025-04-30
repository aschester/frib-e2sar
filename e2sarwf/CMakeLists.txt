set(CMAKE_INSTALL_RPATH ${UFMT_LIB} ${NSCLDAQ_LIB} ${CMAKE_INSTALL_PREFIX}/lib)

###########################################################################
# Common includes/libs:
##

set(common_includes
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${UFMT_INC}
  ${NSCLDAQ_INC}
  ${CMAKE_SOURCE_DIR}/utils
  )
set(common_libs
  ${PROJECT_LIBS}
  ${UFMT_LIBS}
  ${NSCLDAQ_LIB}/libException.so
  ${NSCLDAQ_LIB}/liburl.so
  ${NSCLDAQ_LIB}/libDataFlow.so
  )

set(exec e2sarwf)
set(src
  e2sarwf.cpp CSender.cpp CReceiver.cpp FribE2sarUtils.cpp DataSource.cpp FdDataSource.cpp StreamDataSource.cpp DataSink.cpp FileDataSink.cpp RingDataSink.cpp
  )
set(headers
  CSender.h CReceiver.h FribE2sarUtils.h DataSource.h FdDataSource.h StreamDataSource.h DataSink.h FileDataSink.h RingDataSink.h
  )
add_executable(${exec} ${src})
target_include_directories(${exec} PRIVATE ${common_includes})
target_link_libraries(${exec} ${common_libs})
install(FILES ${headers} DESTINATION include)
install(TARGETS ${exec} DESTINATION bin)
