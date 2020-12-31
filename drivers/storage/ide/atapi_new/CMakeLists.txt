
list(APPEND SOURCE
    atapi.c
    atapi.h
    pdo.c
    fdo.c
    legacy.c)

add_library(atapi2 MODULE ${SOURCE} atapi.rc)

set_module_type(atapi2 kernelmodedriver)
# add_pch(atapi2 atapi.h SOURCE)
add_importlibs(atapi2 ntoskrnl hal wmilib)
add_cd_file(TARGET atapi2 DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_registry_inf(atapi_reg.inf)
