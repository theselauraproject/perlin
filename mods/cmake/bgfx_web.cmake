
set(BGFX_WEB_SRC "${bgfx_SOURCE_DIR}")

add_library(bgfxweb STATIC
    "${BGFX_WEB_SRC}/bx/src/amalgamated.cpp"
    "${BGFX_WEB_SRC}/bgfx/src/amalgamated.cpp"
    "${BGFX_WEB_SRC}/bimg/src/image.cpp"
)
target_include_directories(bgfxweb PUBLIC
    "${BGFX_WEB_SRC}/bgfx/include"
    "${BGFX_WEB_SRC}/bimg/include"
    "${BGFX_WEB_SRC}/bx/include"
)
target_include_directories(bgfxweb PRIVATE
    "${BGFX_WEB_SRC}/bgfx/3rdparty"
)
target_compile_options(bgfxweb PRIVATE -w -pthread)
target_compile_definitions(bgfxweb PRIVATE BIMG_CONFIG_DECODE_ASTC=0)
target_compile_definitions(bgfxweb PUBLIC "BX_CONFIG_DEBUG=$<IF:$<CONFIG:Debug>,1,0>")
target_link_options(bgfxweb PUBLIC -pthread)

set(PERLIN_MBEDTLS_SOURCES
    "${mbedtls_SOURCE_DIR}/library/aes.c"
    "${mbedtls_SOURCE_DIR}/library/bignum.c"
    "${mbedtls_SOURCE_DIR}/library/bignum_core.c"
    "${mbedtls_SOURCE_DIR}/library/bignum_mod.c"
    "${mbedtls_SOURCE_DIR}/library/bignum_mod_raw.c"
    "${mbedtls_SOURCE_DIR}/library/constant_time.c"
    "${mbedtls_SOURCE_DIR}/library/ctr_drbg.c"
    "${mbedtls_SOURCE_DIR}/library/ecdh.c"
    "${mbedtls_SOURCE_DIR}/library/ecdsa.c"
    "${mbedtls_SOURCE_DIR}/library/ecp.c"
    "${mbedtls_SOURCE_DIR}/library/ecp_curves.c"
    "${mbedtls_SOURCE_DIR}/library/ecp_curves_new.c"
    "${mbedtls_SOURCE_DIR}/library/error.c"
    "${mbedtls_SOURCE_DIR}/library/md5.c"
    "${mbedtls_SOURCE_DIR}/library/oid.c"
    "${mbedtls_SOURCE_DIR}/library/platform.c"
    "${mbedtls_SOURCE_DIR}/library/platform_util.c"
    "${mbedtls_SOURCE_DIR}/library/sha256.c"
    "${mbedtls_SOURCE_DIR}/library/sha512.c"
    "${mbedtls_SOURCE_DIR}/library/threading.c"
)
set_source_files_properties(${PERLIN_MBEDTLS_SOURCES} PROPERTIES LANGUAGE C)
