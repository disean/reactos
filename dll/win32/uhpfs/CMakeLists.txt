
spec2def(uhpfs.dll uhpfs.spec)

list(APPEND SOURCE
    uhpfs.c
    uhpfs.rc
    ${CMAKE_CURRENT_BINARY_DIR}/uhpfs.def)

add_library(uhpfs MODULE ${SOURCE})
set_module_type(uhpfs nativedll)
target_link_libraries(uhpfs hpfslib)
add_importlibs(uhpfs ntdll)
add_cd_file(TARGET uhpfs DESTINATION reactos/system32 FOR all)
