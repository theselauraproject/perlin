include_guard(GLOBAL)

if(NOT TARGET conduit::conduit)
    set(PERLIN_CONDUIT_DIR "" CACHE PATH "Local conduit checkout to build instead of fetching it")
    set(PERLIN_CONDUIT_TAG "main" CACHE STRING "conduit revision fetched when PERLIN_CONDUIT_DIR is empty")
    set(CONDUIT_BUILD_TESTS OFF CACHE BOOL "Build the conduit tests")
    set(CONDUIT_INSTALL OFF CACHE BOOL "Generate the conduit install target")
    if(PERLIN_CONDUIT_DIR)
        add_subdirectory("${PERLIN_CONDUIT_DIR}" "${CMAKE_BINARY_DIR}/_deps/conduit-build")
    else()
        include(FetchContent)
        FetchContent_Declare(conduit
            GIT_REPOSITORY https://github.com/theperlinproject/conduit.git
            GIT_TAG ${PERLIN_CONDUIT_TAG}
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(conduit)
    endif()
endif()
