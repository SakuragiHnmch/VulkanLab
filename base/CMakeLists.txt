file(GLOB BASE_SRC "*.cpp" "../3rd/imgui/*.cpp")

set(KTX_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../3rd/ktx)
set(KTX_SOURCES
        ${KTX_DIR}/lib/texture.c
        ${KTX_DIR}/lib/hashlist.c
        ${KTX_DIR}/lib/checkheader.c
        ${KTX_DIR}/lib/swap.c
        ${KTX_DIR}/lib/memstream.c
        ${KTX_DIR}/lib/filestream.c)

add_library(base STATIC ${BASE_SRC} ${KTX_SOURCES})

