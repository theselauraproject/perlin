include_guard(GLOBAL)

set(PERLITE_SOURCE_DIR "" CACHE PATH "Local Perlite checkout to embed instead of fetching it")
set(PERLITE_TAG "main" CACHE STRING "Perlite revision fetched when PERLITE_SOURCE_DIR is empty")

set(PERLITE_EMBED ON CACHE BOOL "" FORCE)
set(PERLITE_BUILD_EXE OFF CACHE BOOL "" FORCE)
set(PERLITE_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(PERLITE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
if(EMSCRIPTEN)
    set(PERLITE_NETHERNET OFF CACHE BOOL "" FORCE)
    set(PERLITE_LEVELDB OFF CACHE BOOL "" FORCE)
endif()

if(PERLITE_SOURCE_DIR)
    get_filename_component(_perlite_dir "${PERLITE_SOURCE_DIR}" ABSOLUTE)
    add_subdirectory("${_perlite_dir}" "${CMAKE_BINARY_DIR}/_deps/perlite-build")
else()
    include(FetchContent)
    FetchContent_Declare(perlite
        GIT_REPOSITORY https://github.com/theperlinproject/perlite.git
        GIT_TAG ${PERLITE_TAG}
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(perlite)
    if(PERLITE_TAG STREQUAL "main")
        message(WARNING "Perlite is tracking the moving 'main' branch; set PERLITE_TAG to a tag or commit for reproducible builds")
    endif()
endif()
