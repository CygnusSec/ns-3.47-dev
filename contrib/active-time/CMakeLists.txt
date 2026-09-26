build_lib(
  LIBNAME active-time
  SOURCE_FILES
    model/measurement/catra-active-time-estimator.cc
    model/measurement/catra-algorithm-packet.cc
    model/measurement/catra-mac-transaction-tracker.cc
    model/measurement/observe-mac-frame.cc
  HEADER_FILES
    model/measurement/catra-active-time-estimator.h
    model/measurement/catra-algorithm-packet.h
    model/measurement/catra-mac-transaction-tracker.h
    model/measurement/observe-mac-frame.h
  LIBRARIES_TO_LINK
    ${libcore}
    ${libnetwork}
    ${libinternet}
    ${libwifi}
)
