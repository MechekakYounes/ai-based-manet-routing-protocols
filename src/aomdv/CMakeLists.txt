# src/aomdv/CMakeLists.txt

build_lib(
    LIBNAME aomdv
    SOURCE_FILES
        helper/aomdv-helper.cc
        model/aomdv-routing-protocol.cc
        model/aomdv-dpd.cc
        model/aomdv-id-cache.cc
        model/aomdv-neighbor.cc
        model/aomdv-packet.cc
        model/aomdv-rqueue.cc
        model/aomdv-rtable.cc
        
    HEADER_FILES
        helper/aomdv-helper.h
        model/aomdv-routing-protocol.h
        model/aomdv-dpd.h
        model/aomdv-id-cache.h
        model/aomdv-neighbor.h
        model/aomdv-packet.h
        model/aomdv-rqueue.h
        model/aomdv-rtable.h
        
    LIBRARIES_TO_LINK
        ${libinternet}
        ${libwifi}
)
