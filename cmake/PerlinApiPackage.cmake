include_guard(GLOBAL)

set(PERLIN_API_SOURCE_DIR "" CACHE PATH "Local perlin-api checkout to build against instead of fetching it")
set(PERLIN_API_TAG "main" CACHE STRING "perlin-api revision fetched when PERLIN_API_SOURCE_DIR is empty")

if(PERLIN_API_SOURCE_DIR)
    get_filename_component(perlin_api_SOURCE_DIR "${PERLIN_API_SOURCE_DIR}" ABSOLUTE)
else()
    include(FetchContent)

    FetchContent_Declare(perlin_api
        GIT_REPOSITORY https://github.com/theperlinproject/perlin-api.git
        GIT_TAG ${PERLIN_API_TAG}
        GIT_SHALLOW TRUE
        SOURCE_SUBDIR no-cmake-project)
    FetchContent_MakeAvailable(perlin_api)
endif()
include("${perlin_api_SOURCE_DIR}/cmake/PerlinApi.cmake")
