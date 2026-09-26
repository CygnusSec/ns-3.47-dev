build_lib(
  LIBNAME catra
  SOURCE_FILES
    model/mac/catra-mac-controller.cc
    model/tcp/catra-tcp-controller.cc
  HEADER_FILES
    model/mac/catra-mac-controller.h
    model/tcp/catra-tcp-controller.h
  LIBRARIES_TO_LINK
    ${libcore}
    ${libnetwork}
    ${libinternet}
    ${libwifi}
)
