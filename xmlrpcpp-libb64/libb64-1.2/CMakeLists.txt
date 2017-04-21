add_library(b64 STATIC src/cencode.c src/cdecode.c)
target_include_directories(b64 PUBLIC "include")
set_target_properties(b64 PROPERTIES POSITION_INDEPENDENT_CODE on)
