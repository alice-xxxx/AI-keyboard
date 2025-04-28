FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/main/app/*.*)
FILE(GLOB_RECURSE audio_sources ${CMAKE_SOURCE_DIR}/main/audio/*.*)
FILE(GLOB_RECURSE display_sources ${CMAKE_SOURCE_DIR}/main/display/*.*)
FILE(GLOB_RECURSE key_sources ${CMAKE_SOURCE_DIR}/main/key/*.*)
idf_component_register(SRCS "main.c"  ${app_sources} ${audio_sources} ${display_sources} ${key_sources}
                    INCLUDE_DIRS "." "./app" "./audio" "./display" "./key"
                    EMBED_FILES root.html)
