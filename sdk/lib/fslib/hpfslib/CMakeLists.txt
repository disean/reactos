
list(APPEND SOURCE
    hpfschk.c
    hpfslib.c
    hpfslib.h)

add_library(hpfslib ${SOURCE})
add_pch(hpfslib hpfslib.h SOURCE)
target_link_libraries(hpfslib chkstk)

if(NOT MSVC)
    target_link_libraries(hpfslib -lgcc)
endif()

add_dependencies(hpfslib psdk)
