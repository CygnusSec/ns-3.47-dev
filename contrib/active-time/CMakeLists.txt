build_lib(
  LIBNAME active-time
  SOURCE_FILES
    model/measurement/active-time-estimator.cc
    model/measurement/active-time-algorithm-packet.cc
    model/measurement/active-time-mac-transaction-tracker.cc
    model/measurement/active-time-observe-mac-frame.cc
  HEADER_FILES
    model/measurement/active-time-estimator.h
    model/measurement/active-time-algorithm-packet.h
    model/measurement/active-time-mac-transaction-tracker.h
    model/measurement/active-time-observe-mac-frame.h
  LIBRARIES_TO_LINK
    ${libcore}
    ${libnetwork}
    ${libinternet}
    ${libwifi}
)
