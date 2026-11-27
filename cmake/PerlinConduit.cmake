include_guard(GLOBAL)

if(NOT TARGET conduit::conduit)
    # Conduit is a separate upstream project. No bundled or patched copy lives in Perlin.
    set(PERLIN_CONDUIT_DIR "" CACHE PATH "Optional separate local Conduit checkout")
    set(PERLIN_CONDUIT_TAG "main" CACHE STRING "Conduit Git commit or tag (defaults to main after Conduit changes are pushed)")
    set(CONDUIT_BUILD_TESTS OFF CACHE BOOL "Build Conduit tests")
    set(CONDUIT_INSTALL OFF CACHE BOOL "Generate Conduit install target")
    if(PERLIN_CONDUIT_DIR)
        add_subdirectory("${PERLIN_CONDUIT_DIR}" "${CMAKE_BINARY_DIR}/_deps/conduit-build")
    else()
        include(FetchContent)
        FetchContent_Declare(conduit
            GIT_REPOSITORY https://github.com/theperlinproject/conduit.git
            GIT_TAG ${PERLIN_CONDUIT_TAG})
        FetchContent_MakeAvailable(conduit)
    endif()
endif()
