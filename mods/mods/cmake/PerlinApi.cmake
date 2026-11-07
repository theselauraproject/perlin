include_guard(GLOBAL)

get_filename_component(PERLIN_API_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(PERLIN_API_SCHEMA_DIR "${PERLIN_API_DIR}/api")
set(PERLIN_API_SDK_DIR "${PERLIN_API_DIR}/sdk")
set(PERLIN_API_INCLUDE_DIRS "${PERLIN_API_DIR}/sdk" "${PERLIN_API_DIR}")
set(PERLIN_API_HOST_CORE_SOURCES "${PERLIN_API_DIR}/host/PluginCore.cpp")
set(PERLIN_API_HOST_WASMEDGE_SOURCES "${PERLIN_API_DIR}/host/WasmRuntime.cpp")
set(PERLIN_API_HOST_WASMEDGE_STOP_SOURCES "${PERLIN_API_DIR}/host/WasmEdgeStop.cpp")
set(PERLIN_API_HOST_WASM3_SOURCES "${PERLIN_API_DIR}/host/WasmRuntimeWasm3.cpp")

file(GLOB PERLIN_API_SCHEMA_FILES CONFIGURE_DEPENDS "${PERLIN_API_SCHEMA_DIR}/*.papi")
file(GLOB_RECURSE PERLIN_API_CODEGEN_FILES CONFIGURE_DEPENDS "${PERLIN_API_DIR}/codegen/*.py")

if(NOT TARGET perlin_api)
    add_library(perlin_api INTERFACE)
    target_include_directories(perlin_api INTERFACE ${PERLIN_API_INCLUDE_DIRS})
    target_compile_features(perlin_api INTERFACE cxx_std_23)
endif()

find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_FOUND AND NOT TARGET perlin_api_codegen)
    add_custom_target(perlin_api_codegen
        COMMAND ${Python3_EXECUTABLE} "${PERLIN_API_DIR}/codegen/generate.py"
        WORKING_DIRECTORY "${PERLIN_API_DIR}"
        SOURCES ${PERLIN_API_SCHEMA_FILES}
        COMMENT "Regenerating the Perlin mod ABI from mods/api"
        VERBATIM)
endif()

function(perlin_api_add_checks)
    if(Python3_FOUND)
        add_test(NAME perlin_api_codegen_check
            COMMAND ${Python3_EXECUTABLE} "${PERLIN_API_DIR}/codegen/generate.py" --check)
        add_test(NAME perlin_api_host_check
            COMMAND ${Python3_EXECUTABLE} "${PERLIN_API_DIR}/tools/check_api.py")
    endif()
endfunction()
