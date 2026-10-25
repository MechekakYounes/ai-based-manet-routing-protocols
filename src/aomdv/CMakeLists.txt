build_lib(
  LIBNAME aomdv
  SOURCE_FILES
    model/aomdv-routing-protocol.cc
    model/aomdv-rtable.cc
    model/aomdv-packet.cc
    helper/aomdv-helper.cc
  HEADER_FILES
    model/aomdv-routing-protocol.h
    model/aomdv-rtable.h
    model/aomdv-packet.h
    helper/aomdv-helper.h
  LIBRARIES_TO_LINK
    ${libinternet}
    ${libwifi}
    ${libnetwork}
    ${libcore}
)
