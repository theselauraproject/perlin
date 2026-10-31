cmake_minimum_required(VERSION 3.28)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "" FORCE)
    # Never pick up Homebrew's dynamic fmt: it would be linked as /opt/homebrew/.../libfmt.12.dylib and missing on other Macs.
    set(CMAKE_DISABLE_FIND_PACKAGE_fmt ON CACHE BOOL "" FORCE)
endif()

option(PERLIN_USE_CLANG_CL "Build with clang-cl on Windows" ON)
if(CMAKE_HOST_WIN32 AND PERLIN_USE_CLANG_CL AND NOT CMAKE_GENERATOR MATCHES "Visual Studio"
   AND NOT CMAKE_C_COMPILER AND NOT CMAKE_CXX_COMPILER AND NOT DEFINED ENV{CC} AND NOT DEFINED ENV{CXX})
    set(_perlin_llvm_hints "$ENV{VCINSTALLDIR}/Tools/Llvm/x64/bin" "$ENV{ProgramFiles}/LLVM/bin" "C:/Program Files/LLVM/bin")
    find_program(PERLIN_CLANG_CL clang-cl HINTS ${_perlin_llvm_hints})
    find_program(PERLIN_LLD_LINK lld-link HINTS ${_perlin_llvm_hints})
    if(PERLIN_CLANG_CL)
        set(CMAKE_C_COMPILER "${PERLIN_CLANG_CL}" CACHE FILEPATH "C compiler" FORCE)
        set(CMAKE_CXX_COMPILER "${PERLIN_CLANG_CL}" CACHE FILEPATH "C++ compiler" FORCE)
        if(PERLIN_LLD_LINK)
            set(CMAKE_LINKER "${PERLIN_LLD_LINK}" CACHE FILEPATH "Linker" FORCE) # needed for LTO
        endif()
        message(STATUS "Perlin: using clang-cl (${PERLIN_CLANG_CL})")
    else()
        message(WARNING "Perlin: clang-cl not found, falling back to cl.exe. Install \"C++ Clang tools for Windows\" "
                        "in the Visual Studio Installer, or LLVM, to build with clang-cl.")
    endif()
endif()

project(Perlin VERSION 1.0.0 LANGUAGES C CXX)

if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    set(PERLIN_CLANG_CL_BUILD ON)
    message(STATUS "Perlin: compiler is clang-cl ${CMAKE_CXX_COMPILER_VERSION}")
else()
    set(PERLIN_CLANG_CL_BUILD OFF)
endif()

set(CMAKE_POLICY_VERSION_MINIMUM 3.10)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

if(WIN32)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_compile_definitions(GLM_FORCE_DEPTH_ZERO_TO_ONE)

set(PERLIN_BUILD_APP ON)

if(EMSCRIPTEN)
    add_compile_options(-pthread -fwasm-exceptions)
    string(REPLACE "." ";" PERLIN_EMSCRIPTEN_VERSION_PARTS "${EMSCRIPTEN_VERSION}")
    list(GET PERLIN_EMSCRIPTEN_VERSION_PARTS 0 PERLIN_EM_MAJOR)
    list(GET PERLIN_EMSCRIPTEN_VERSION_PARTS 1 PERLIN_EM_MINOR)
    list(GET PERLIN_EMSCRIPTEN_VERSION_PARTS 2 PERLIN_EM_TINY)
    string(REGEX REPLACE "[^0-9].*" "" PERLIN_EM_TINY "${PERLIN_EM_TINY}")
    add_compile_definitions(
        __EMSCRIPTEN_MAJOR__=${PERLIN_EM_MAJOR}
        __EMSCRIPTEN_MINOR__=${PERLIN_EM_MINOR}
        __EMSCRIPTEN_TINY__=${PERLIN_EM_TINY})
    add_link_options(-pthread -fwasm-exceptions)
endif()

set(PERLIN_ANDROID_OPENSSL_BASE "" CACHE PATH "Directory holding one OpenSSL install per Android ABI")
if(ANDROID AND PERLIN_ANDROID_OPENSSL_BASE AND NOT OPENSSL_ROOT_DIR)
    set(OPENSSL_ROOT_DIR "${PERLIN_ANDROID_OPENSSL_BASE}/${ANDROID_ABI}" CACHE PATH "OpenSSL root" FORCE)
endif()

if(WIN32 AND NOT OPENSSL_ROOT_DIR)
    foreach(root "$ENV{VCPKG_INSTALLATION_ROOT}" "$ENV{VCPKG_ROOT}" "C:/Users/judah/vcpkg")
        if(root AND EXISTS "${root}/installed/x64-windows-static/include/openssl")
            set(OPENSSL_ROOT_DIR "${root}/installed/x64-windows-static" CACHE PATH "OpenSSL root")
            break()
        endif()
    endforeach()
endif()

if(APPLE AND NOT OPENSSL_ROOT_DIR)
    # Homebrew's OpenSSL is built for the runner's macOS (15), so linking it at target 12.0 warns and can crash on
    # older systems. CI passes -DOPENSSL_ROOT_DIR to a copy built with -mmacosx-version-min=12.0; this is only a local fallback.
    message(WARNING "OPENSSL_ROOT_DIR not set: falling back to Homebrew OpenSSL (built for a newer macOS than ${CMAKE_OSX_DEPLOYMENT_TARGET})")
    foreach(candidate /opt/homebrew/opt/openssl@3 /usr/local/opt/openssl@3)
        if(EXISTS "${candidate}")
            set(OPENSSL_ROOT_DIR "${candidate}" CACHE PATH "OpenSSL root")
            break()
        endif()
    endforeach()
endif()

if(OPENSSL_ROOT_DIR)
    file(TO_CMAKE_PATH "${OPENSSL_ROOT_DIR}" OPENSSL_ROOT_DIR)
endif()

# Static by default; distros without libcrypto.a (e.g. Arch) pass -DOPENSSL_USE_STATIC_LIBS=OFF.
if(NOT DEFINED OPENSSL_USE_STATIC_LIBS)
    set(OPENSSL_USE_STATIC_LIBS ON)
endif()
if(WIN32)
    set(OPENSSL_MSVC_STATIC_RT ON)
endif()
if((EMSCRIPTEN OR ANDROID) AND OPENSSL_ROOT_DIR)
    list(APPEND CMAKE_FIND_ROOT_PATH "${OPENSSL_ROOT_DIR}")
endif()
if(PERLIN_BUILD_APP)
    find_package(OpenSSL REQUIRED)
    find_package(Threads REQUIRED)
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)

include(cmake/CPM.cmake)
include(cmake/Materials.cmake)

if(PERLIN_BUILD_APP)
CPMAddPackage("gh:stephenberry/glaze@7.1.1")
CPMAddPackage("gh:cameron314/concurrentqueue@1.0.4")
CPMAddPackage("gh:g-truc/glm#1.0.1")
CPMAddPackage(NAME stb GITHUB_REPOSITORY nothings/stb GIT_TAG master DOWNLOAD_ONLY YES)
if(EMSCRIPTEN)
CPMAddPackage(NAME wasm3 GITHUB_REPOSITORY wasm3/wasm3 GIT_TAG 28ecb9af6d2040e474a70f7cb7f43666740141fb DOWNLOAD_ONLY YES)
set(WASM3_CORE_SOURCES m3_bind.c m3_code.c m3_compile.c m3_core.c m3_env.c m3_exec.c m3_function.c m3_info.c m3_module.c m3_parse.c m3_validate.c)
list(TRANSFORM WASM3_CORE_SOURCES PREPEND "${wasm3_SOURCE_DIR}/source/")
add_library(wasm3_core STATIC ${WASM3_CORE_SOURCES})
target_include_directories(wasm3_core PUBLIC "${wasm3_SOURCE_DIR}/source")
target_compile_definitions(wasm3_core PRIVATE d_m3HasTracer=0)
if(NOT MSVC)
    target_compile_options(wasm3_core PRIVATE -w -mtail-call)
endif()
set_target_properties(wasm3_core PROPERTIES POSITION_INDEPENDENT_CODE ON C_STANDARD 11)
set(PERLIN_WASM_ENGINE wasm3_core)
else()
CPMAddPackage(NAME wasmedge GITHUB_REPOSITORY WasmEdge/WasmEdge GIT_TAG 0.18.0 DOWNLOAD_ONLY YES)
# libdatachannel temporarily enables this cache option later in configuration.
# An interrupted configure must not turn WasmEdge's hidden internal targets into
# shared libraries on the next run.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(WASMEDGE_BUILD_SHARED_LIB OFF CACHE BOOL "" FORCE)
set(WASMEDGE_BUILD_STATIC_LIB OFF CACHE BOOL "" FORCE)
set(WASMEDGE_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(WASMEDGE_BUILD_PLUGINS OFF CACHE BOOL "" FORCE)
set(WASMEDGE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WASMEDGE_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(WASMEDGE_USE_LLVM OFF CACHE BOOL "" FORCE)
if(ANDROID)
    set(WASMEDGE_FORCE_DISABLE_LTO ON CACHE BOOL "" FORCE)
else()
    set(WASMEDGE_FORCE_DISABLE_LTO OFF CACHE BOOL "" FORCE)
endif()
set(WASMEDGE_PLUGIN_WASI_LOGGING OFF CACHE BOOL "" FORCE)
get_property(perlin_job_pools GLOBAL PROPERTY JOB_POOLS)
if(NOT perlin_job_pools MATCHES "(^|;)link=")
    set_property(GLOBAL APPEND PROPERTY JOB_POOLS link=2)
endif()
add_subdirectory("${wasmedge_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/_deps/wasmedge-build" EXCLUDE_FROM_ALL)
function(perlin_quiet_targets dir)
    get_property(targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(t IN LISTS targets)
        get_target_property(type ${t} TYPE)
        if(NOT type STREQUAL "INTERFACE_LIBRARY" AND NOT type STREQUAL "UTILITY")
            if(WIN32)
                set_property(TARGET ${t} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
            endif()
            if(MSVC)
                target_compile_options(${t} PRIVATE /WX- /W0)
            else()
                target_compile_options(${t} PRIVATE -Wno-error -w)
            endif()
        endif()
    endforeach()
    get_property(subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    foreach(d IN LISTS subdirs)
        perlin_quiet_targets("${d}")
    endforeach()
endfunction()
perlin_quiet_targets("${wasmedge_SOURCE_DIR}")
add_library(perlin_wasmedge STATIC
    "${wasmedge_SOURCE_DIR}/lib/api/wasmedge.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/client/mods/WasmEdgeStop.cpp")
target_compile_definitions(perlin_wasmedge PUBLIC WASMEDGE_COMPILE_LIBRARY)
target_include_directories(perlin_wasmedge PUBLIC
    "${CMAKE_BINARY_DIR}/_deps/wasmedge-build/include/api"
    "${wasmedge_SOURCE_DIR}/include/api")
target_link_libraries(perlin_wasmedge PUBLIC wasmedgeVM wasmedgeDriver)
set_target_properties(perlin_wasmedge PROPERTIES CXX_STANDARD 17 CXX_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(perlin_wasmedge PRIVATE /utf-8 /bigobj /W0)
else()
    target_compile_options(perlin_wasmedge PRIVATE -w)
endif()
set(PERLIN_WASM_ENGINE perlin_wasmedge)
endif()
if(NOT EMSCRIPTEN)
CPMAddPackage(NAME httplib GITHUB_REPOSITORY yhirose/cpp-httplib GIT_TAG v0.60.1 DOWNLOAD_ONLY YES)
endif()
endif()

if(PERLIN_BUILD_APP AND NOT EMSCRIPTEN)
set(DATACHANNEL_TESTS OFF CACHE BOOL "" FORCE)
set(DBUILD_TESTS OFF CACHE BOOL "" FORCE)
set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
set(NO_TESTS ON CACHE BOOL "" FORCE)
set(LIBSRTP_TEST_APPS OFF CACHE BOOL "" FORCE)
set(ENABLE_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
# position independent: the static library is also linked into the singleplayer server library
set(PERLIN_SAVED_PIC "${CMAKE_POSITION_INDEPENDENT_CODE}")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
CPMAddPackage(
    NAME libdatachannel
    GITHUB_REPOSITORY paullouisageneau/libdatachannel
    GIT_TAG v0.23.2
    EXCLUDE_FROM_ALL YES
    OPTIONS "USE_GNUTLS OFF" "USE_NICE OFF" "NO_TESTS ON" "NO_EXAMPLES ON"
)
set(CMAKE_POSITION_INDEPENDENT_CODE "${PERLIN_SAVED_PIC}")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
endif()

if(PERLIN_BUILD_APP)
CPMAddPackage(NAME miniz GITHUB_REPOSITORY richgel999/miniz GIT_TAG 3.0.2)

if(EMSCRIPTEN)
    set(SDL_PTHREADS ON CACHE BOOL "" FORCE)
endif()
if(ANDROID)
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
else()
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
endif()
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
CPMAddPackage(NAME SDL3 GITHUB_REPOSITORY libsdl-org/SDL GIT_TAG release-3.4.18)

set(RMLUI_FONT_ENGINE none CACHE STRING "" FORCE)
set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
set(RMLUI_TESTS OFF CACHE BOOL "" FORCE)
set(RMLUI_PRECOMPILED_HEADERS OFF CACHE BOOL "" FORCE)
set(RMLUI_COMPILER_OPTIONS OFF CACHE BOOL "" FORCE)
set(RMLUI_THIRDPARTY_CONTAINERS OFF CACHE BOOL "" FORCE)
CPMAddPackage(
    NAME RmlUi
    GITHUB_REPOSITORY mikke89/RmlUi
    GIT_TAG 6.1
    GIT_SHALLOW YES
)
endif()

# Java (OptiFine / Iris) shader packs: GLSL -> SPIR-V (glslang) -> GLSL / HLSL (SPIRV-Cross).
CPMAddPackage(NAME glslang GITHUB_REPOSITORY KhronosGroup/glslang GIT_TAG e2bb10631198bc9d73daf752d9170608d5e2c8bd GIT_SHALLOW NO DOWNLOAD_ONLY YES)
CPMAddPackage(NAME SPIRV-Cross GITHUB_REPOSITORY KhronosGroup/SPIRV-Cross GIT_TAG 300681bbcb8365a8d1eacdacdbdd0249bbf1d6b1 GIT_SHALLOW NO DOWNLOAD_ONLY YES)
include(cmake/PerlinJavaShaders.cmake)

# graphics apis per platform: the first one the device supports is picked at run time, the others stay switchable in game
function(perlin_default_api name value)
    if(NOT DEFINED ${name})
        set(${name} ${value} CACHE BOOL "build this rhi backend")
    endif()
endfunction()
if(EMSCRIPTEN)
    perlin_default_api(PERLIN_RHI_VULKAN OFF)
    perlin_default_api(PERLIN_RHI_D3D12 OFF)
    perlin_default_api(PERLIN_RHI_D3D11 OFF)
    perlin_default_api(PERLIN_RHI_METAL OFF)
    perlin_default_api(PERLIN_RHI_WEBGPU ON)
elseif(ANDROID)
    perlin_default_api(PERLIN_RHI_VULKAN ON)
    perlin_default_api(PERLIN_RHI_D3D12 OFF)
    perlin_default_api(PERLIN_RHI_D3D11 OFF)
    perlin_default_api(PERLIN_RHI_METAL OFF)
    perlin_default_api(PERLIN_RHI_WEBGPU OFF)
elseif(WIN32)
    perlin_default_api(PERLIN_RHI_VULKAN ON)
    perlin_default_api(PERLIN_RHI_D3D12 ON)
    perlin_default_api(PERLIN_RHI_D3D11 ON)
    perlin_default_api(PERLIN_RHI_METAL OFF)
    perlin_default_api(PERLIN_RHI_WEBGPU OFF)
elseif(APPLE)
    perlin_default_api(PERLIN_RHI_VULKAN OFF)
    perlin_default_api(PERLIN_RHI_D3D12 OFF)
    perlin_default_api(PERLIN_RHI_D3D11 OFF)
    perlin_default_api(PERLIN_RHI_METAL ON)
    perlin_default_api(PERLIN_RHI_WEBGPU OFF)
else()
    perlin_default_api(PERLIN_RHI_VULKAN ON)
    perlin_default_api(PERLIN_RHI_D3D12 OFF)
    perlin_default_api(PERLIN_RHI_D3D11 OFF)
    perlin_default_api(PERLIN_RHI_METAL OFF)
    perlin_default_api(PERLIN_RHI_WEBGPU OFF)
endif()

include(cmake/PerlinShaders.cmake)

# graphics layer: rhi backends, chunk system, and the engine's gpu context on top of them
set(PERLIN_GLM_DIR "${glm_SOURCE_DIR}" CACHE PATH "" FORCE)
add_subdirectory(src/rhi)
add_subdirectory(src/render)
add_subdirectory(src/client/renderer/gpu)

set(PERLIN_PROTOCOL_OUT "${CMAKE_CURRENT_BINARY_DIR}/generated/protocol")
set(PERLIN_PROTOCOL_VERSION 2225)
set(PERLIN_PROTOCOL_CODEC_COUNT 6)
set(PERLIN_PROTOCOL_SOURCES)
foreach(i RANGE 0 5)
    list(APPEND PERLIN_PROTOCOL_SOURCES "${PERLIN_PROTOCOL_OUT}/Codec${i}.cpp")
endforeach()
set(PERLIN_PROTOCOL_HEADERS "${PERLIN_PROTOCOL_OUT}/Types.hpp" "${PERLIN_PROTOCOL_OUT}/Packets.hpp")
set(PERLIN_PROTOCOL_ROUNDTRIP "${PERLIN_PROTOCOL_OUT}/RoundTrip.cpp")
set(PERLIN_PROTOCOL_JSON "${PERLIN_PROTOCOL_OUT}/PacketJson.cpp")

add_custom_command(
    OUTPUT ${PERLIN_PROTOCOL_HEADERS} ${PERLIN_PROTOCOL_SOURCES} ${PERLIN_PROTOCOL_ROUNDTRIP} ${PERLIN_PROTOCOL_JSON}
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/protogen/protogen.py"
            --spec "${CMAKE_CURRENT_SOURCE_DIR}/protocol/bedrock-1.26.60.json"
            --overrides "${CMAKE_CURRENT_SOURCE_DIR}/protocol/overrides.json"
            --out "${PERLIN_PROTOCOL_OUT}"
            --parts ${PERLIN_PROTOCOL_CODEC_COUNT}
            --namespace proto${PERLIN_PROTOCOL_VERSION}
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/protogen/protogen.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/protocol/bedrock-1.26.60.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/protocol/overrides.json"
    COMMENT "Generating protocol codecs"
    VERBATIM)

add_library(perlin_protocol STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/network/Nbt.cpp"
    ${PERLIN_PROTOCOL_SOURCES}
    ${PERLIN_PROTOCOL_HEADERS})
target_include_directories(perlin_protocol PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_features(perlin_protocol PUBLIC cxx_std_23)
set_target_properties(perlin_protocol PROPERTIES ARCHIVE_OUTPUT_NAME perlin_protocol_gen)
if(MSVC)
    set_source_files_properties(${PERLIN_PROTOCOL_SOURCES} PROPERTIES COMPILE_OPTIONS "/w;/bigobj;$<$<CONFIG:Debug>:/O1>")
else()
    set_source_files_properties(${PERLIN_PROTOCOL_SOURCES} PROPERTIES COMPILE_OPTIONS "-w;$<$<CONFIG:Debug>:-O1>")
endif()

# Numerical table read from the running Windows 1.26.52.3 server. Generate a
# host-endian-independent initializer; executable bytes are never embedded.
set(PERLIN_CURRENT_SINE "${CMAKE_CURRENT_SOURCE_DIR}/src/client/player/data/perlin_sine_1_26_52.bin")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PERLIN_CURRENT_SINE}")
file(READ "${PERLIN_CURRENT_SINE}" PERLIN_CURRENT_SINE_HEX HEX)
string(LENGTH "${PERLIN_CURRENT_SINE_HEX}" PERLIN_CURRENT_SINE_LENGTH)
if(NOT PERLIN_CURRENT_SINE_LENGTH EQUAL 524288)
    message(FATAL_ERROR "Current sine table must contain 65,536 binary32 words")
endif()
string(REGEX REPLACE "(..)(..)(..)(..)" "0x\\4\\3\\2\\1u," PERLIN_CURRENT_SINE_WORDS "${PERLIN_CURRENT_SINE_HEX}")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/PerlinCurrentSineTable.inc"
    CONTENT "${PERLIN_CURRENT_SINE_WORDS}\n")

file(GLOB PERLIN_RESOURCE_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/*")
list(APPEND PERLIN_RESOURCE_FILES "${PERLIN_SHADER_PACK}")
set(PERLIN_EMBED_DIR "${CMAKE_CURRENT_BINARY_DIR}/embedded")
file(MAKE_DIRECTORY "${PERLIN_EMBED_DIR}")
set(PERLIN_EMBED_NAMES_SOURCE "")
if(MSVC)
    enable_language(RC)
    set(PERLIN_EMBED_SOURCE "${PERLIN_EMBED_DIR}/embedded_resources.rc")
    set(PERLIN_EMBED_NAMES_SOURCE "${PERLIN_EMBED_DIR}/embedded_names.cpp")
    set(text "")
    set(names "// Generated by CMake: names of the RCDATA resources (resource id = index + 1).\n#include <cstddef>\n")
    string(APPEND names "extern \"C\" {\nextern const char* const perlin_embedded_names[] = {\n")
    set(i 0)
    foreach(f IN LISTS PERLIN_RESOURCE_FILES)
        get_filename_component(n "${f}" NAME)
        math(EXPR id "${i}+1")
        string(APPEND text "${id} RCDATA \"${f}\"\n")
        string(APPEND names "    \"${n}\",\n")
        math(EXPR i "${i}+1")
    endforeach()
    string(APPEND names "    nullptr};\nextern const size_t perlin_embedded_count = ${i};\n}\n")
    file(GENERATE OUTPUT "${PERLIN_EMBED_NAMES_SOURCE}" CONTENT "${names}")
else()
    set(PERLIN_EMBED_SOURCE "${PERLIN_EMBED_DIR}/embedded_resources.cpp")
    set(endsection ".previous")
    if(EMSCRIPTEN)
        set(sym "")
        set(section ".section .rodata.perlin,\\\"\\\",@")
        set(endsection ".text")
    elseif(APPLE)
        set(sym "_")
        set(section ".section __DATA,__const")
    elseif(WIN32)
        set(sym "")
        set(section ".section .rdata,\\\"dr\\\"")
    else()
        set(sym "")
        set(section ".section .rodata")
    endif()
    set(text "// Generated by CMake: resources/ linked into the executable.\n#include <cstddef>\n#include <cstdint>\n")
    string(APPEND text "asm(\n\"${section}\\n\"\n\".balign 16\\n\"\n")
    set(table "")
    set(decls "")
    set(i 0)
    foreach(f IN LISTS PERLIN_RESOURCE_FILES)
        get_filename_component(n "${f}" NAME)
        string(APPEND text "\".globl ${sym}perlin_res_${i}\\n${sym}perlin_res_${i}:\\n.incbin \\\"${f}\\\"\\n.globl ${sym}perlin_res_${i}_end\\n${sym}perlin_res_${i}_end:\\n.balign 16\\n\"\n")
        string(APPEND decls "extern const uint8_t perlin_res_${i}[]; extern const uint8_t perlin_res_${i}_end[];\n")
        string(APPEND table "    {\"${n}\", perlin_res_${i}, perlin_res_${i}_end},\n")
        math(EXPR i "${i}+1")
    endforeach()
    string(APPEND text "\"${endsection}\\n\"\n);\nextern \"C\" {\n${decls}")
    string(APPEND text "struct PerlinEmbeddedFile { const char* name; const uint8_t* begin; const uint8_t* end; };\n")
    string(APPEND text "extern const PerlinEmbeddedFile perlin_embedded_files[] = {\n${table}    {nullptr, nullptr, nullptr}};\n")
    string(APPEND text "extern const size_t perlin_embedded_count = ${i};\n}\n")
endif()
file(GENERATE OUTPUT "${PERLIN_EMBED_SOURCE}" CONTENT "${text}")
set_source_files_properties("${PERLIN_EMBED_SOURCE}" PROPERTIES GENERATED TRUE OBJECT_DEPENDS "${PERLIN_RESOURCE_FILES}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${PERLIN_RESOURCE_FILES})

file(GLOB_RECURSE PERLIN_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
list(FILTER PERLIN_SOURCES EXCLUDE REGEX "/src/network/Nbt\\.cpp")
list(FILTER PERLIN_SOURCES EXCLUDE REGEX "/src/client/renderer/(bgfx|shader)/") # stale leftovers from the pre-RHI tree
list(FILTER PERLIN_SOURCES EXCLUDE REGEX "/src/client/renderer/gpu/")
list(FILTER PERLIN_SOURCES EXCLUDE REGEX "/src/rhi/")
list(FILTER PERLIN_SOURCES EXCLUDE REGEX "/src/render/")
list(FILTER PERLIN_SOURCES EXCLUDE REGEX "/src/client/javashaders/") # built into perlin_gpu
list(FILTER PERLIN_SOURCES EXCLUDE REGEX "/src/mods/")
list(FILTER PERLIN_SOURCES EXCLUDE REGEX "/src/(src|tests)/")
list(FILTER PERLIN_SOURCES EXCLUDE REGEX "/src/client/mods/WasmEdgeStop\\.cpp$")
if(EMSCRIPTEN)
    list(FILTER PERLIN_SOURCES EXCLUDE REGEX "/src/client/mods/WasmRuntime\\.cpp$")
else()
    list(FILTER PERLIN_SOURCES EXCLUDE REGEX "/src/client/mods/WasmRuntimeWasm3\\.cpp$")
endif()
if(EMSCRIPTEN)
    list(FILTER PERLIN_SOURCES EXCLUDE REGEX "/src/network/(NetherNet|Http|RakNet)\\.cpp")
else()
    list(FILTER PERLIN_SOURCES EXCLUDE REGEX "_web\\.cpp$")
endif()

if(ANDROID)
    add_library(Perlin SHARED ${PERLIN_SOURCES} ${PERLIN_EMBED_SOURCE} ${PERLIN_EMBED_NAMES_SOURCE})
    set_target_properties(Perlin PROPERTIES OUTPUT_NAME main)
elseif(WIN32)
    enable_language(RC)
    set(PERLIN_ICON_RC "${CMAKE_CURRENT_BINARY_DIR}/perlin_icon.rc")
    file(WRITE "${PERLIN_ICON_RC}" "101 ICON \"${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/perlin.ico\"\n")
    set_source_files_properties("${PERLIN_ICON_RC}" PROPERTIES OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/perlin.ico")
    add_executable(Perlin WIN32 ${PERLIN_SOURCES} ${PERLIN_EMBED_SOURCE} ${PERLIN_EMBED_NAMES_SOURCE} "${PERLIN_ICON_RC}")
elseif(APPLE)
    set(PERLIN_ICNS "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/Perlin.icns")
    set_source_files_properties("${PERLIN_ICNS}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    add_executable(Perlin MACOSX_BUNDLE ${PERLIN_SOURCES} ${PERLIN_EMBED_SOURCE} ${PERLIN_EMBED_NAMES_SOURCE} "${PERLIN_ICNS}")
    set_target_properties(Perlin PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "Perlin"
        MACOSX_BUNDLE_GUI_IDENTIFIER "io.perlin.client"
        MACOSX_BUNDLE_ICON_FILE "Perlin.icns"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")
else()
    add_executable(Perlin ${PERLIN_SOURCES} ${PERLIN_EMBED_SOURCE} ${PERLIN_EMBED_NAMES_SOURCE})
endif()

target_compile_features(Perlin PRIVATE cxx_std_23)
# Every packet decoded to JSON for the runtime packet log; one big switch, so compiled quietly.
target_sources(Perlin PRIVATE ${PERLIN_PROTOCOL_JSON})
if(MSVC)
    set_source_files_properties(${PERLIN_PROTOCOL_JSON} PROPERTIES COMPILE_OPTIONS "/w;/bigobj")
else()
    set_source_files_properties(${PERLIN_PROTOCOL_JSON} PROPERTIES COMPILE_OPTIONS "-w")
endif()

target_include_directories(Perlin PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/mods/sdk"
    "${stb_SOURCE_DIR}"
    "${concurrentqueue_SOURCE_DIR}"
    "${glm_SOURCE_DIR}"
)
if(NOT EMSCRIPTEN)
    target_include_directories(Perlin PRIVATE "${httplib_SOURCE_DIR}" "${libdatachannel_SOURCE_DIR}/include")
endif()

target_compile_definitions(Perlin PRIVATE PERLIN_SOURCE_DIR="${CMAKE_SOURCE_DIR}" PERLIN_VERSION="${PROJECT_VERSION}"
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    OPENSSL_SUPPRESS_DEPRECATED
)
if(NOT EMSCRIPTEN)
    target_compile_definitions(Perlin PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)
endif()

if(WIN32)
    target_compile_definitions(Perlin PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
endif()

target_link_libraries(Perlin PRIVATE
    perlin_glslang perlin_spirv_cross
    ${PERLIN_WASM_ENGINE}
    perlin_protocol
    glaze::glaze
    miniz
    OpenSSL::Crypto
    Threads::Threads
    SDL3::SDL3
    rmlui_core
    perlin_gpu
)
add_dependencies(Perlin perlin_shader_pack)
if(NOT EMSCRIPTEN)
    target_link_libraries(Perlin PRIVATE datachannel-static OpenSSL::SSL)
endif()

if(EMSCRIPTEN)
    set(PERLIN_WEB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/web")
    set_target_properties(Perlin PROPERTIES OUTPUT_NAME index SUFFIX ".html")
    target_link_options(Perlin PRIVATE
        -sWASMFS=1
        -sFORCE_FILESYSTEM=1
        -sMAX_WEBGL_VERSION=2
        -sMIN_WEBGL_VERSION=2
        -sFULL_ES3=1
        -sALLOW_MEMORY_GROWTH=1
        -sINITIAL_MEMORY=1024MB
        -sMAXIMUM_MEMORY=4GB
        -sSTACK_SIZE=8MB
        -sDEFAULT_PTHREAD_STACK_SIZE=2MB
        -sPTHREAD_POOL_SIZE=20
        -sPTHREAD_POOL_SIZE_STRICT=0
        -sFETCH=1
        -sEXIT_RUNTIME=0
        -sASSERTIONS=0
        -sENVIRONMENT=web,worker
        -sTEXTDECODER=1
        "-sEXPORTED_FUNCTIONS=_main,_malloc,_free,_perlin_nn_on_message,_perlin_nn_on_closed"
        "-sEXPORTED_RUNTIME_METHODS=FS,callMain,ccall,cwrap,UTF8ToString,stringToUTF8,lengthBytesUTF8"
        "--js-library=${PERLIN_WEB_DIR}/library_nethernet.js"
        "--shell-file=${PERLIN_WEB_DIR}/shell.html")
    option(PERLIN_WEB_PROFILING "Keep function names and enable runtime checks in the web build" OFF)
    if(PERLIN_WEB_PROFILING)
        target_link_options(Perlin PRIVATE --profiling-funcs -sASSERTIONS=1 -sSTACK_OVERFLOW_CHECK=2)
    endif()
    set_property(TARGET Perlin APPEND PROPERTY LINK_DEPENDS "${PERLIN_WEB_DIR}/library_nethernet.js" "${PERLIN_WEB_DIR}/shell.html")
    add_custom_command(TARGET Perlin POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${PERLIN_WEB_DIR}/static" "$<TARGET_FILE_DIR:Perlin>")
elseif(WIN32)
    target_link_libraries(Perlin PRIVATE ws2_32 crypt32 dbghelp advapi32 user32 gdi32 bcrypt iphlpapi)
elseif(APPLE)
    target_link_libraries(Perlin PRIVATE "-framework Security" "-framework CoreFoundation")
    target_compile_definitions(Perlin PRIVATE CPPHTTPLIB_USE_CERTS_FROM_MACOSX_KEYCHAIN)
elseif(ANDROID)
    target_link_libraries(Perlin PRIVATE log android)
    target_link_options(Perlin PRIVATE -Wl,--build-id=none -Wl,-z,max-page-size=16384)
elseif(NOT EMSCRIPTEN)
    target_link_libraries(Perlin PRIVATE dl)
    target_link_options(Perlin PRIVATE -static-libstdc++ -static-libgcc)
endif()

option(PERLIN_DEBUG_OPT "Optimise Perlin's own sources in Debug builds" ON)
if(PERLIN_DEBUG_OPT)
    if(MSVC)
        set_source_files_properties(${PERLIN_SOURCES} PROPERTIES COMPILE_OPTIONS "$<$<CONFIG:Debug>:/O2;/Ob2>")
    else()
        set_source_files_properties(${PERLIN_SOURCES} PROPERTIES COMPILE_OPTIONS "$<$<CONFIG:Debug>:-O2>")
    endif()
endif()

# The BDS reference uses separate scalar SSE operations. Contracting multiply
# and add changes physics branch decisions, including auto-step acceptance.
# Keep these rules even when PERLIN_FAST_MATH relaxes rendering calculations.
foreach(PERLIN_PHYSICS_SOURCE IN ITEMS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/client/player/PlayerPhysics.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/client/player/VehiclePhysics.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/client/player/LocalPlayer.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/client/player/PlayerFov.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/client/resources/BlockOffsetDefinition.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/client/resources/BlockBoxDefinition.cpp")
    if(MSVC)
        set_property(SOURCE "${PERLIN_PHYSICS_SOURCE}" APPEND PROPERTY COMPILE_OPTIONS /fp:strict)
    else()
        set_property(SOURCE "${PERLIN_PHYSICS_SOURCE}" APPEND PROPERTY COMPILE_OPTIONS
            -fno-fast-math -ffp-contract=off)
    endif()
endforeach()

if(MSVC)
    target_compile_options(Perlin PRIVATE /W4 /permissive- /EHsc /Zc:__cplusplus)
    if(PERLIN_CLANG_CL_BUILD)
        target_compile_options(Perlin PRIVATE -Wno-unused-parameter -Wno-microsoft-cast -Wno-microsoft-enum-value
                               -Wno-language-extension-token -Wno-c++98-compat -Wno-pragma-pack)
    endif()
    target_compile_options(Perlin PRIVATE /Zi)
    target_link_options(Perlin PRIVATE /DEBUG /OPT:REF /OPT:ICF)
    string(REGEX REPLACE "[/-]RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    set_property(TARGET Perlin PROPERTY MSVC_RUNTIME_CHECKS "")
    file(GLOB_RECURSE PERLIN_HOT_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/client/renderer/chunk/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/client/multiplayer/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/network/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/world/*.cpp")
    set_source_files_properties(${PERLIN_HOT_SOURCES} PROPERTIES COMPILE_OPTIONS "/O2;/Ob2")
else()
    target_compile_options(Perlin PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

if(MSVC AND NOT PERLIN_CLANG_CL_BUILD)
    target_compile_options(Perlin PRIVATE "$<$<CONFIG:Release>:/Gy;/Gw;/Zc:inline;/Oi>")
elseif(PERLIN_CLANG_CL_BUILD)
    target_compile_options(Perlin PRIVATE "$<$<CONFIG:Release>:/Gy;/Gw;/Zc:inline;-fno-math-errno;-fno-trapping-math>")
else()
    target_compile_options(Perlin PRIVATE "$<$<CONFIG:Release>:-fno-math-errno;-fno-trapping-math;-ffunction-sections;-fdata-sections>")
    if(NOT APPLE AND NOT EMSCRIPTEN)
        target_link_options(Perlin PRIVATE "$<$<CONFIG:Release>:-Wl,--gc-sections;-Wl,--as-needed>")
    endif()
endif()

option(PERLIN_FAST_MATH "Relax floating-point rules (faster maths, results differ in the last digits)" OFF)
if(PERLIN_FAST_MATH)
    if(MSVC AND NOT PERLIN_CLANG_CL_BUILD)
        target_compile_options(Perlin PRIVATE /fp:fast)
    else()
        target_compile_options(Perlin PRIVATE -fno-signed-zeros -freciprocal-math -ffp-contract=fast)
    endif()
    message(STATUS "Perlin: fast math is ON")
endif()

# The build intention rejects non-finite rays and velocities; keep those guards when the application enables
# relaxed floating point.
if(MSVC AND NOT PERLIN_CLANG_CL_BUILD)
    set_source_files_properties(src/client/player/Bridging.cpp src/client/player/HeldBuild.cpp PROPERTIES COMPILE_OPTIONS "/fp:strict")
else()
    set_source_files_properties(src/client/player/Bridging.cpp src/client/player/HeldBuild.cpp PROPERTIES COMPILE_OPTIONS "-fno-fast-math;-ffp-contract=off")
endif()

include(CheckIPOSupported)
if(EMSCRIPTEN OR ANDROID)
    set(PERLIN_IPO OFF)
else()
    check_ipo_supported(RESULT PERLIN_IPO OUTPUT PERLIN_IPO_LOG)
endif()
if(PERLIN_IPO)
    set_property(TARGET Perlin PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        set_source_files_properties(src/client/entity/EntityRenderer.cpp PROPERTIES COMPILE_OPTIONS "-fno-lto")
    elseif(MSVC)
        set_source_files_properties(src/client/entity/EntityRenderer.cpp PROPERTIES COMPILE_OPTIONS "/GL-")
    endif()
endif()

if(NOT ANDROID)
    install(TARGETS Perlin RUNTIME DESTINATION . BUNDLE DESTINATION .)
endif()
if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)
    install(FILES packaging/linux/io.perlin.client.desktop DESTINATION share/applications)
    install(DIRECTORY packaging/linux/icons/ DESTINATION share/icons/hicolor)
endif()
# FFmpeg libraries compiled straight into Perlin: video export then needs no ffmpeg program at all.
# Windows CI installs a static build with vcpkg (see .github/workflows/build.yml) and passes -DFFMPEG_ROOT=...;
# Linux/macOS use pkg-config. If nothing is found Perlin still builds and falls back to its built-in AVI encoder.
option(PERLIN_LIBAV "Link FFmpeg libraries into Perlin for built-in video encoding" ON)
# Windows skips FFmpeg entirely by default (slow to build/download; video export uses the built-in AVI encoder).
option(PERLIN_WINDOWS_FFMPEG "Link/bundle FFmpeg on Windows" OFF)
if(WIN32 AND NOT PERLIN_WINDOWS_FFMPEG)
    set(PERLIN_LIBAV OFF)
    set(PERLIN_BUNDLE_FFMPEG OFF CACHE BOOL "" FORCE)
endif()
set(PERLIN_LIBAV_FOUND OFF)
if(PERLIN_LIBAV AND NOT EMSCRIPTEN AND NOT ANDROID)
    set(FFMPEG_ROOT "" CACHE PATH "Prefix containing include/libavcodec and lib/ with static FFmpeg libraries")
    if(FFMPEG_ROOT AND EXISTS "${FFMPEG_ROOT}/include/libavcodec/avcodec.h")
        file(GLOB _av_libs "${FFMPEG_ROOT}/lib/*.lib" "${FFMPEG_ROOT}/lib/*.a")
        # drop libraries that are not part of the codec stack (test helpers, pkgconfig dirs are not matched anyway)
        if(_av_libs)
            target_include_directories(Perlin PRIVATE "${FFMPEG_ROOT}/include")
            target_link_libraries(Perlin PRIVATE ${_av_libs})
            set(PERLIN_LIBAV_FOUND ON)
        endif()
    elseif(NOT WIN32 AND NOT APPLE)
        # Not on macOS: pkg-config finds Homebrew's *dynamic* FFmpeg, which drags in libfmt/libx264/... dylibs that
        # do not exist on other Macs ("libfmt.12.dylib not found") and are built for a newer macOS than 12.
        # Pass -DFFMPEG_ROOT=<static prefix built with -mmacosx-version-min=12.0> to link FFmpeg on macOS.
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(PERLIN_AV QUIET IMPORTED_TARGET libavformat libavcodec libswscale libavutil)
            if(PERLIN_AV_FOUND)
                target_link_libraries(Perlin PRIVATE PkgConfig::PERLIN_AV)
                set(PERLIN_LIBAV_FOUND ON)
            endif()
        endif()
    endif()
    if(PERLIN_LIBAV_FOUND)
        target_compile_definitions(Perlin PRIVATE PERLIN_HAVE_LIBAV)
        if(WIN32)
            target_link_libraries(Perlin PRIVATE ws2_32 secur32 bcrypt ole32 oleaut32 uuid user32 mfuuid mfplat strmiids shlwapi ncrypt advapi32 gdi32 psapi)
        endif()
        message(STATUS "FFmpeg libraries found: video encoders are built into Perlin")
    else()
        message(STATUS "FFmpeg libraries not found: using ffmpeg executable if present, else the built-in AVI encoder")
    endif()
endif()

# Ship ffmpeg next to perlin.exe on Windows so video export works with nothing to install.
# The LGPL build is used (no GPL obligations on redistribution); Perlin also has a built-in
# AVI/MJPEG encoder as a last resort and can download the full GPL build from the export dialog.
if(WIN32 AND NOT EMSCRIPTEN AND NOT ANDROID AND NOT PERLIN_LIBAV_FOUND)
    option(PERLIN_BUNDLE_FFMPEG "Download an LGPL ffmpeg build and ship it next to perlin.exe" ON)
    # The download is kept in a fixed per-user cache so it happens once, not on every fresh build directory.
    if(PERLIN_BUNDLE_FFMPEG)
        if(DEFINED ENV{LOCALAPPDATA})
            set(PERLIN_FFMPEG_DIR "$ENV{LOCALAPPDATA}/PerlinBuildCache/ffmpeg-bundle")
        else()
            set(PERLIN_FFMPEG_DIR "${CMAKE_BINARY_DIR}/ffmpeg-bundle")
        endif()
        if(NOT EXISTS "${PERLIN_FFMPEG_DIR}/ffmpeg.exe")
            set(_ff_zip "${CMAKE_BINARY_DIR}/ffmpeg-lgpl.zip")
            message(STATUS "Downloading ffmpeg (LGPL build) for bundling...")
            file(DOWNLOAD
                "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-lgpl.zip"
                "${_ff_zip}" STATUS _ff_status TLS_VERIFY ON)
            list(GET _ff_status 0 _ff_code)
            if(_ff_code EQUAL 0)
                file(ARCHIVE_EXTRACT INPUT "${_ff_zip}" DESTINATION "${CMAKE_BINARY_DIR}/ffmpeg-extract" PATTERNS "*/bin/ffmpeg.exe" "*/LICENSE.txt")
                file(GLOB_RECURSE _ff_exe "${CMAKE_BINARY_DIR}/ffmpeg-extract/*/bin/ffmpeg.exe")
                if(_ff_exe)
                    list(GET _ff_exe 0 _ff_exe_one)
                    file(COPY "${_ff_exe_one}" DESTINATION "${PERLIN_FFMPEG_DIR}")
                endif()
                file(REMOVE "${_ff_zip}")
            else()
                message(WARNING "Could not download ffmpeg for bundling (${_ff_status}); Perlin will use its built-in encoder or download it at runtime.")
            endif()
        endif()
        if(EXISTS "${PERLIN_FFMPEG_DIR}/ffmpeg.exe")
            add_custom_command(TARGET Perlin POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${PERLIN_FFMPEG_DIR}/ffmpeg.exe" "$<TARGET_FILE_DIR:Perlin>/ffmpeg.exe")
            install(FILES "${PERLIN_FFMPEG_DIR}/ffmpeg.exe" DESTINATION .)
        endif()
    endif()
endif()
option(PERLIN_BUILD_TESTS "Build unit tests" OFF)
if(PERLIN_BUILD_TESTS AND NOT EMSCRIPTEN AND NOT ANDROID)
    enable_testing()
    add_executable(skin_lifetime_selftest tests/skin_lifetime_selftest.cpp src/client/entity/EntityTracker.cpp)
    target_include_directories(skin_lifetime_selftest PRIVATE src "${glm_SOURCE_DIR}")
    target_compile_features(skin_lifetime_selftest PRIVATE cxx_std_23)
    add_test(NAME skin_lifetime_selftest COMMAND skin_lifetime_selftest)
    add_executable(block_palette_selftest tests/block_palette_selftest.cpp
        src/world/level/block/BlockPalette.cpp src/util/Json.cpp)
    target_include_directories(block_palette_selftest PRIVATE src)
    target_link_libraries(block_palette_selftest PRIVATE glaze::glaze)
    target_compile_features(block_palette_selftest PRIVATE cxx_std_23)
    add_test(NAME block_palette_selftest COMMAND block_palette_selftest "${CMAKE_CURRENT_SOURCE_DIR}/resources/block_states.json")
    add_executable(entity_render_selftest tests/entity_render_selftest.cpp
        src/client/entity/EntityQuery.cpp src/client/entity/EntityTracker.cpp src/client/entity/BlockEntityView.cpp
        src/client/multiplayer/BlockEntities.cpp src/client/attachable/Geometry.cpp
        src/client/attachable/Animation.cpp src/client/attachable/Molang.cpp src/client/ui/Json.cpp
        src/world/level/block/BlockPalette.cpp src/util/Json.cpp)
    target_include_directories(entity_render_selftest PRIVATE src "${glm_SOURCE_DIR}")
    target_link_libraries(entity_render_selftest PRIVATE glaze::glaze)
    target_compile_features(entity_render_selftest PRIVATE cxx_std_23)
    add_test(NAME entity_render_selftest COMMAND entity_render_selftest)
    add_executable(player_fov_selftest tests/player_fov_selftest.cpp src/client/player/PlayerFov.cpp)
    target_include_directories(player_fov_selftest PRIVATE src)
    target_compile_features(player_fov_selftest PRIVATE cxx_std_23)
    add_test(NAME player_fov_selftest COMMAND player_fov_selftest)
    add_executable(block_box_definition_selftest tests/block_box_definition_selftest.cpp
        src/client/resources/BlockBoxDefinition.cpp src/client/ui/Json.cpp)
    target_include_directories(block_box_definition_selftest PRIVATE src "${glm_SOURCE_DIR}")
    target_compile_features(block_box_definition_selftest PRIVATE cxx_std_23)
    add_test(NAME block_box_definition_selftest COMMAND block_box_definition_selftest)
    add_executable(hud_dump tests/hud_dump.cpp src/client/ui/Json.cpp src/client/ui/UiExpr.cpp src/client/ui/JsonUi.cpp
        src/client/ui/UiLayout.cpp src/client/ui/McFont.cpp src/client/ui/TenFont.cpp src/client/ui/RmlEmit.cpp src/client/ui/FormatCodes.cpp src/client/ui/HudData.cpp
        src/client/ui/HudRenderers.cpp src/client/ui/EffectData.cpp src/client/i18n/Lang.cpp src/client/resources/ServerPackRegistry.cpp src/client/i18n/BuiltinEnglish.cpp src/util/Resources.cpp src/util/Paths.cpp)
    target_include_directories(hud_dump PRIVATE src "${stb_SOURCE_DIR}")
    target_compile_features(hud_dump PRIVATE cxx_std_23)
    add_test(NAME hud_boss_bindings COMMAND hud_dump --boss-selftest)
    add_executable(screen_dump tests/screen_dump.cpp src/client/ui/Json.cpp src/client/ui/UiExpr.cpp src/client/ui/JsonUi.cpp
        src/client/ui/UiLayout.cpp src/client/ui/McFont.cpp src/client/ui/TenFont.cpp src/client/ui/RmlEmit.cpp src/client/ui/FormatCodes.cpp src/client/ui/HudData.cpp src/client/ui/EffectData.cpp
        src/client/i18n/Lang.cpp src/client/resources/ServerPackRegistry.cpp src/client/i18n/BuiltinEnglish.cpp src/util/Resources.cpp src/util/Paths.cpp)
    target_include_directories(screen_dump PRIVATE src "${stb_SOURCE_DIR}")
    target_compile_features(screen_dump PRIVATE cxx_std_23)
    foreach(size small normal large extra_large)
        add_test(NAME ui_font_${size} COMMAND screen_dump "font_sizes.${size}_screen" 320 240
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ui_font_sizes")
        set_tests_properties(ui_font_${size} PROPERTIES ENVIRONMENT "SCREEN_TREE=1;SCREEN_RML=1")
    endforeach()
    set_tests_properties(ui_font_small PROPERTIES PASS_REGULAR_EXPRESSION "sample .*12.0x4.5")
    set_tests_properties(ui_font_normal PROPERTIES PASS_REGULAR_EXPRESSION "sample .*24.0x9.0")
    set_tests_properties(ui_font_large PROPERTIES PASS_REGULAR_EXPRESSION "sample .*48.0x18.0")
    set_tests_properties(ui_font_extra_large PROPERTIES PASS_REGULAR_EXPRESSION "sample .*96.0x36.0")
    add_test(NAME ui_bound_alpha COMMAND screen_dump font_sizes.alpha_screen 320 240
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ui_font_sizes")
    set_tests_properties(ui_bound_alpha PROPERTIES ENVIRONMENT "SCREEN_RML=1;SCREEN_VARS={\"#opacity\":0.3}"
        PASS_REGULAR_EXPRESSION "alpha=\"0[.]30*\"")
    add_test(NAME ui_repeated_routes COMMAND screen_dump routes.routing_screen 320 240
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ui_routes")
    set_tests_properties(ui_repeated_routes PROPERTIES ENVIRONMENT "SCREEN_TREE=1;SCREEN_VARS={\"#title_text\":\"middle\"}"
        PASS_REGULAR_EXPRESSION "text=EXPECTED_MIDDLE_ROUTE"
        FAIL_REGULAR_EXPRESSION "text=WRONG_FIRST_ROUTE;text=WRONG_LAST_ROUTE")
    add_test(NAME ui_fractional_font COMMAND screen_dump font_sizes.fractional_screen 320 240
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ui_font_sizes")
    set_tests_properties(ui_fractional_font PROPERTIES ENVIRONMENT "SCREEN_RML=1"
        PASS_REGULAR_EXPRESSION "font-family:minecraft-size-6[.]4")
    add_test(NAME ui_stack_vertical COMMAND screen_dump stack_regression.vertical_screen 320 240
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ui_stack")
    set_tests_properties(ui_stack_vertical PROPERTIES ENVIRONMENT "SCREEN_TREE=1"
        PASS_REGULAR_EXPRESSION "content.*320[.]0x221[.]0")
    add_test(NAME ui_stack_horizontal COMMAND screen_dump stack_regression.horizontal_screen 320 240
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ui_stack")
    set_tests_properties(ui_stack_horizontal PROPERTIES ENVIRONMENT "SCREEN_TREE=1"
        PASS_REGULAR_EXPRESSION "content.*301[.]0x240[.]0")
    add_test(NAME ui_deferred_text COMMAND screen_dump regression.deferred_screen 320 240
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ui_deferred")
    set_tests_properties(ui_deferred_text PROPERTIES ENVIRONMENT "SCREEN_TREE=1"
        PASS_REGULAR_EXPRESSION "text=deferred-label-correct")
    add_executable(gpu_level_unload_selftest tests/gpu_context_test.cpp tests/gpu_context_stubs.cpp
        src/client/renderer/Tessellator.cpp src/client/renderer/SkyGeometry.cpp src/client/renderer/MaterialDb.cpp
        src/client/ui/Json.cpp src/util/Paths.cpp src/util/Resources.cpp)
    target_sources(gpu_level_unload_selftest PRIVATE
        src/client/javashaders/PackSource.cpp src/client/javashaders/PackSourceZip.cpp
        src/client/javashaders/Properties.cpp src/client/javashaders/Cond.cpp
        src/client/resources/TextureLoader.cpp src/world/level/block/BlockPalette.cpp src/util/Json.cpp)
    target_include_directories(gpu_level_unload_selftest PRIVATE "${stb_SOURCE_DIR}")
    target_link_libraries(gpu_level_unload_selftest PRIVATE glaze::glaze miniz)
    target_include_directories(gpu_level_unload_selftest BEFORE PRIVATE tests/stubs src "${glm_SOURCE_DIR}")
    target_link_libraries(gpu_level_unload_selftest PRIVATE perlin_gpu)
    add_test(NAME gpu_level_unload_selftest COMMAND gpu_level_unload_selftest "${PERLIN_SHADER_PACK}")
    set_tests_properties(gpu_level_unload_selftest PROPERTIES SKIP_RETURN_CODE 77)
    add_test(NAME java_to_slang_selftest COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/java_to_slang_selftest.py")
    add_executable(ten_font_metrics_selftest tests/ten_font_metrics_selftest.cpp
        src/client/ui/TenFont.cpp src/client/ui/McFont.cpp)
    target_include_directories(ten_font_metrics_selftest PRIVATE src "${stb_SOURCE_DIR}")
    target_compile_features(ten_font_metrics_selftest PRIVATE cxx_std_23)
    add_test(NAME ten_font_metrics_selftest COMMAND ten_font_metrics_selftest)
    set_tests_properties(ten_font_metrics_selftest PROPERTIES SKIP_RETURN_CODE 77)
    add_executable(font_cache_selftest tests/font_cache_selftest.cpp src/client/ui/McFont.cpp)
    target_include_directories(font_cache_selftest PRIVATE src "${stb_SOURCE_DIR}")
    target_compile_features(font_cache_selftest PRIVATE cxx_std_23)
    add_test(NAME font_cache_selftest COMMAND font_cache_selftest)
    add_executable(ui_library_selftest tests/ui_library_selftest.cpp src/client/ui/Json.cpp
        src/client/ui/UiExpr.cpp src/client/ui/JsonUi.cpp)
    target_include_directories(ui_library_selftest PRIVATE src)
    target_compile_features(ui_library_selftest PRIVATE cxx_std_23)
    add_test(NAME ui_library_selftest COMMAND ui_library_selftest)
    add_executable(ui_conformance tests/ui_conformance.cpp src/client/ui/Json.cpp src/client/ui/UiExpr.cpp src/client/ui/JsonUi.cpp
        src/client/ui/UiLayout.cpp src/client/ui/McFont.cpp src/client/ui/TenFont.cpp src/client/ui/RmlEmit.cpp
        src/client/ui/HudData.cpp src/client/ui/FormatCodes.cpp src/client/i18n/Lang.cpp
        src/client/i18n/BuiltinEnglish.cpp src/client/resources/ServerPackRegistry.cpp src/util/Resources.cpp src/util/Paths.cpp)
    target_include_directories(ui_conformance PRIVATE src "${stb_SOURCE_DIR}" "${glm_SOURCE_DIR}")
    target_compile_features(ui_conformance PRIVATE cxx_std_23)
    add_test(NAME ui_screen_discovery COMMAND ui_conformance
        "regression:${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ui_deferred")
    set_tests_properties(ui_screen_discovery PROPERTIES
        PASS_REGULAR_EXPRESSION "screens=2 +built=2 +failed=0 +missing=0")
    add_executable(targeting_selftest tests/targeting_selftest.cpp)
    target_include_directories(targeting_selftest PRIVATE src "${glm_SOURCE_DIR}")
    target_compile_features(targeting_selftest PRIVATE cxx_std_23)
    add_test(NAME targeting_selftest COMMAND targeting_selftest)
    add_executable(hand_anim_selftest tests/hand_anim_selftest.cpp)
    target_include_directories(hand_anim_selftest PRIVATE src)
    target_compile_features(hand_anim_selftest PRIVATE cxx_std_23)
    add_test(NAME hand_anim_selftest COMMAND hand_anim_selftest)
    add_executable(bridging_selftest
        tests/bridging_selftest.cpp
        src/client/player/Bridging.cpp
        src/client/player/HeldBuild.cpp
        src/client/player/PlayerPhysics.cpp
        src/client/player/BlockUse.cpp)
    target_include_directories(bridging_selftest PRIVATE src "${glm_SOURCE_DIR}")
    target_compile_features(bridging_selftest PRIVATE cxx_std_23)
    add_test(NAME bridging_selftest COMMAND bridging_selftest)
    add_executable(entity_animation_selftest tests/entity_animation_selftest.cpp
        src/client/entity/JavaAnimation.cpp src/client/attachable/Geometry.cpp
        src/client/attachable/Animation.cpp src/client/attachable/Molang.cpp src/client/ui/Json.cpp)
    target_include_directories(entity_animation_selftest PRIVATE src "${glm_SOURCE_DIR}")
    target_compile_features(entity_animation_selftest PRIVATE cxx_std_23)
    add_test(NAME entity_animation_selftest COMMAND entity_animation_selftest
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/java_animation_reference.txt"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/java_animation_root_reference.txt")
    add_executable(attachable_selftest tests/attachable_selftest.cpp
        src/client/attachable/Molang.cpp src/client/attachable/Animation.cpp src/client/attachable/Geometry.cpp
        src/client/ui/Json.cpp)
    target_include_directories(attachable_selftest PRIVATE src "${glm_SOURCE_DIR}")
    target_compile_features(attachable_selftest PRIVATE cxx_std_23)
    add_test(NAME attachable_selftest COMMAND attachable_selftest)

    add_executable(particle_selftest tests/particle_selftest.cpp src/client/particle/DataParticles.cpp
        src/client/attachable/Molang.cpp src/client/ui/Json.cpp)
    target_include_directories(particle_selftest PRIVATE src "${glm_SOURCE_DIR}")
    target_compile_features(particle_selftest PRIVATE cxx_std_23)
    add_test(NAME particle_selftest COMMAND particle_selftest)

    add_executable(audio_selftest tests/audio_selftest.cpp src/client/audio/AudioEngine.cpp src/client/audio/Decode.cpp
        src/client/audio/SoundSystem.cpp src/client/audio/Subtitles.cpp src/client/ui/Json.cpp
        src/client/i18n/Lang.cpp src/client/resources/ServerPackRegistry.cpp src/client/i18n/BuiltinEnglish.cpp src/util/Resources.cpp src/util/Paths.cpp)
    target_compile_definitions(audio_selftest PRIVATE PERLIN_LANG_FILE="${CMAKE_SOURCE_DIR}/resources/perlin_en_US.lang")
    target_include_directories(audio_selftest PRIVATE src "${glm_SOURCE_DIR}" "${stb_SOURCE_DIR}")
    target_link_libraries(audio_selftest PRIVATE SDL3::SDL3)
    target_compile_features(audio_selftest PRIVATE cxx_std_23)
    add_test(NAME audio_selftest COMMAND audio_selftest)

    add_executable(lang_selftest tests/lang_selftest.cpp src/client/i18n/Lang.cpp src/client/resources/ServerPackRegistry.cpp src/client/i18n/BuiltinEnglish.cpp src/client/ui/Json.cpp src/util/Resources.cpp src/util/Paths.cpp)
    target_include_directories(lang_selftest PRIVATE src)
    target_compile_features(lang_selftest PRIVATE cxx_std_23)
    add_test(NAME lang_selftest COMMAND lang_selftest "${CMAKE_SOURCE_DIR}/resources")

    add_executable(material_selftest tests/material_selftest.cpp src/client/renderer/MaterialDb.cpp src/client/entity/Materials.cpp src/client/ui/Json.cpp)
    target_include_directories(material_selftest PRIVATE src)
    target_compile_features(material_selftest PRIVATE cxx_std_23)
    add_test(NAME material_selftest COMMAND material_selftest)
    add_executable(replay_selftest
        tests/replay_selftest.cpp
        src/client/replay/ReplayFormat.cpp
        src/client/replay/ReplayController.cpp
        src/client/ui/Json.cpp
        src/util/Paths.cpp)
    target_include_directories(replay_selftest PRIVATE src)
    target_link_libraries(replay_selftest PRIVATE miniz Threads::Threads)
    target_compile_features(replay_selftest PRIVATE cxx_std_23)
    add_test(NAME replay_selftest COMMAND replay_selftest)

    add_executable(ffmpeg_selftest
        tests/ffmpeg_selftest.cpp
        tests/stb_write_impl.cpp
        src/client/replay/AviWriter.cpp
        src/client/replay/Ffmpeg.cpp
        src/client/replay/LibavEncoder.cpp
        src/client/replay/Process.cpp
        src/util/Paths.cpp)
    target_include_directories(ffmpeg_selftest PRIVATE src "${stb_SOURCE_DIR}")
    target_link_libraries(ffmpeg_selftest PRIVATE miniz Threads::Threads)
    target_compile_features(ffmpeg_selftest PRIVATE cxx_std_23)

    add_executable(timeline_selftest
        tests/timeline_selftest.cpp
        src/client/replay/Timeline.cpp
        src/client/ui/Json.cpp)
    target_include_directories(timeline_selftest PRIVATE src)
    target_compile_features(timeline_selftest PRIVATE cxx_std_23)
    add_test(NAME timeline_selftest COMMAND timeline_selftest)

    add_executable(level_undo_selftest
        tests/level_undo_selftest.cpp
        src/world/level/Level.cpp)
    target_include_directories(level_undo_selftest PRIVATE src)
    target_compile_features(level_undo_selftest PRIVATE cxx_std_23)
    add_test(NAME level_undo_selftest COMMAND level_undo_selftest)

    add_executable(chunk_selftest
        tests/chunk_selftest.cpp
        src/world/level/chunk/ChunkCodec.cpp)
    target_link_libraries(chunk_selftest PRIVATE perlin_protocol)
    add_test(NAME chunk_selftest COMMAND chunk_selftest)

    add_executable(packet_selftest tests/packet_selftest.cpp ${PERLIN_PROTOCOL_ROUNDTRIP})
    target_link_libraries(packet_selftest PRIVATE perlin_protocol)
    add_test(NAME packet_selftest COMMAND packet_selftest)

    add_executable(touch_input_selftest tests/touch_input_selftest.cpp
        src/client/input/TouchControls.cpp src/client/input/Input.cpp
        src/client/input/Keys.cpp src/client/input/KeyMapping.cpp)
    target_include_directories(touch_input_selftest PRIVATE src "${concurrentqueue_SOURCE_DIR}")
    target_compile_features(touch_input_selftest PRIVATE cxx_std_23)
    add_test(NAME touch_input_selftest COMMAND touch_input_selftest)
    add_executable(input_selftest
        tests/input_selftest.cpp
        src/client/input/Keys.cpp
        src/client/input/KeyMapping.cpp
        src/client/renderer/Environment.cpp
        src/client/renderer/Fog.cpp
        src/client/renderer/Lightmap.cpp
        src/client/renderer/SkyGeometry.cpp)
    target_include_directories(input_selftest PRIVATE src "${glm_SOURCE_DIR}")
    add_test(NAME input_selftest COMMAND input_selftest)



    add_executable(physics_selftest
        tests/physics_selftest.cpp
        src/client/player/PlayerPhysics.cpp)
    target_include_directories(physics_selftest PRIVATE src)
    target_compile_features(physics_selftest PRIVATE cxx_std_23)
    add_test(NAME physics_selftest COMMAND physics_selftest)

    add_executable(bds_numeric_selftest tests/bds_numeric_selftest.cpp)
    target_include_directories(bds_numeric_selftest PRIVATE src)
    target_compile_features(bds_numeric_selftest PRIVATE cxx_std_23)
    add_test(NAME bds_numeric_selftest COMMAND bds_numeric_selftest
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/bds_numeric.txt"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/bds_sine.bin")

    add_executable(bds_collision_selftest tests/bds_collision_selftest.cpp)
    target_include_directories(bds_collision_selftest PRIVATE src)
    target_compile_features(bds_collision_selftest PRIVATE cxx_std_23)
    add_test(NAME bds_collision_selftest COMMAND bds_collision_selftest
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/bds_collision.txt"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/bds_step.txt"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/bds_broadphase.txt")

    add_executable(bds_vertical_selftest tests/bds_vertical_selftest.cpp)
    target_include_directories(bds_vertical_selftest PRIVATE src)
    target_compile_features(bds_vertical_selftest PRIVATE cxx_std_23)
    add_test(NAME bds_vertical_selftest COMMAND bds_vertical_selftest
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/bds_vertical.txt")

    add_executable(bds_water_drag_selftest tests/bds_water_drag_selftest.cpp)
    target_include_directories(bds_water_drag_selftest PRIVATE src)
    target_compile_features(bds_water_drag_selftest PRIVATE cxx_std_23)
    add_test(NAME bds_water_drag_selftest COMMAND bds_water_drag_selftest
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/bds_water_drag.txt"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/bds_water_acceleration.txt")

    add_executable(bds_movement_effects_selftest tests/bds_movement_effects_selftest.cpp)
    target_include_directories(bds_movement_effects_selftest PRIVATE src)
    target_compile_features(bds_movement_effects_selftest PRIVATE cxx_std_23)
    add_test(NAME bds_movement_effects_selftest COMMAND bds_movement_effects_selftest
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/bds_movement_effects.txt")

    add_executable(bds_gliding_selftest tests/bds_gliding_selftest.cpp)
    target_include_directories(bds_gliding_selftest PRIVATE src)
    target_compile_features(bds_gliding_selftest PRIVATE cxx_std_23)
    add_test(NAME bds_gliding_selftest COMMAND bds_gliding_selftest
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/bds_rotation.txt"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/bds_gliding.txt")

    add_executable(bds_gliding_trace_selftest tests/bds_gliding_trace_selftest.cpp
        src/client/player/PlayerPhysics.cpp)
    target_include_directories(bds_gliding_trace_selftest PRIVATE src)
    target_compile_features(bds_gliding_trace_selftest PRIVATE cxx_std_23)
    add_test(NAME bds_gliding_trace_selftest COMMAND bds_gliding_trace_selftest
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/bds_gliding_sequence.txt")

    add_executable(bds_trig_selftest tests/bds_trig_selftest.cpp)
    target_include_directories(bds_trig_selftest PRIVATE src)
    target_compile_features(bds_trig_selftest PRIVATE cxx_std_23)
    add_test(NAME bds_trig_selftest COMMAND bds_trig_selftest
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/bds_trig.txt"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/bds_relative_movement.txt")

    add_executable(bds_finalization_selftest tests/bds_finalization_selftest.cpp)
    target_include_directories(bds_finalization_selftest PRIVATE src)
    target_compile_features(bds_finalization_selftest PRIVATE cxx_std_23)
    add_test(NAME bds_finalization_selftest COMMAND bds_finalization_selftest
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/bds_finalization.txt")

    add_executable(bds_landing_selftest tests/bds_landing_selftest.cpp)
    target_include_directories(bds_landing_selftest PRIVATE src)
    target_compile_features(bds_landing_selftest PRIVATE cxx_std_23)
    add_test(NAME bds_landing_selftest COMMAND bds_landing_selftest
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/bds_landing.txt")

    add_executable(bds_support_selftest tests/bds_support_selftest.cpp)
    target_include_directories(bds_support_selftest PRIVATE src)
    target_compile_features(bds_support_selftest PRIVATE cxx_std_23)
    add_test(NAME bds_support_selftest COMMAND bds_support_selftest
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/bds_support.txt")

    add_executable(bds_world_collision_selftest tests/bds_world_collision_selftest.cpp)
    target_include_directories(bds_world_collision_selftest PRIVATE src)
    target_compile_features(bds_world_collision_selftest PRIVATE cxx_std_23)
    add_test(NAME bds_world_collision_selftest COMMAND bds_world_collision_selftest
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/bds_unavailable_chunks.txt")

    add_executable(bds_terrain_query_selftest tests/bds_terrain_query_selftest.cpp)
    target_include_directories(bds_terrain_query_selftest PRIVATE src)
    target_compile_features(bds_terrain_query_selftest PRIVATE cxx_std_23)
    add_test(NAME bds_terrain_query_selftest COMMAND bds_terrain_query_selftest
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/bds_terrain_query.txt")

    add_executable(bds_block_geometry_selftest tests/bds_block_geometry_selftest.cpp)
    target_include_directories(bds_block_geometry_selftest PRIVATE src)
    target_compile_features(bds_block_geometry_selftest PRIVATE cxx_std_23)
    add_test(NAME bds_block_geometry_selftest COMMAND bds_block_geometry_selftest
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/bds_block_geometry.txt")

    add_executable(voxel_shapes_selftest tests/voxel_shapes_selftest.cpp src/client/player/VoxelShapes.cpp)
    target_include_directories(voxel_shapes_selftest PRIVATE src)
    target_link_libraries(voxel_shapes_selftest PRIVATE perlin_protocol)
    add_test(NAME voxel_shapes_selftest COMMAND voxel_shapes_selftest)

    add_executable(bds_block_offset_selftest tests/bds_block_offset_selftest.cpp)
    target_include_directories(bds_block_offset_selftest PRIVATE src)
    target_compile_features(bds_block_offset_selftest PRIVATE cxx_std_23)
    add_test(NAME bds_block_offset_selftest COMMAND bds_block_offset_selftest
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/bds_block_offset.txt")

    add_executable(block_offset_definition_selftest tests/block_offset_definition_selftest.cpp
        src/client/resources/BlockOffsetDefinition.cpp src/client/resources/ServerBlocks.cpp
        src/client/ui/Json.cpp)
    target_include_directories(block_offset_definition_selftest PRIVATE src)
    target_compile_features(block_offset_definition_selftest PRIVATE cxx_std_23)
    add_test(NAME block_offset_definition_selftest COMMAND block_offset_definition_selftest
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/bds_block_offset.txt")

    add_executable(bds_collision_cache_selftest tests/bds_collision_cache_selftest.cpp)
    target_include_directories(bds_collision_cache_selftest PRIVATE src)
    target_compile_features(bds_collision_cache_selftest PRIVATE cxx_std_23)
    add_test(NAME bds_collision_cache_selftest COMMAND bds_collision_cache_selftest
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/bds_collision_cache.txt")

    add_executable(bds_cache_difference_selftest tests/bds_cache_difference_selftest.cpp)
    target_include_directories(bds_cache_difference_selftest PRIVATE src)
    target_compile_features(bds_cache_difference_selftest PRIVATE cxx_std_23)
    add_test(NAME bds_cache_difference_selftest COMMAND bds_cache_difference_selftest
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/bds_cache_difference.txt")

    add_executable(bds_cache_refresh_selftest tests/bds_cache_refresh_selftest.cpp)
    target_include_directories(bds_cache_refresh_selftest PRIVATE src)
    target_compile_features(bds_cache_refresh_selftest PRIVATE cxx_std_23)
    add_test(NAME bds_cache_refresh_selftest COMMAND bds_cache_refresh_selftest
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/bds_cache_refresh.txt")

    add_executable(bds_cache_refresh_records_selftest tests/bds_cache_refresh_records_selftest.cpp)
    target_include_directories(bds_cache_refresh_records_selftest PRIVATE src)
    target_compile_features(bds_cache_refresh_records_selftest PRIVATE cxx_std_23)
    add_test(NAME bds_cache_refresh_records_selftest COMMAND bds_cache_refresh_records_selftest
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/bds_cache_refresh_records.txt")

    add_executable(bds_block_slowdown_selftest tests/bds_block_slowdown_selftest.cpp)
    target_include_directories(bds_block_slowdown_selftest PRIVATE src)
    target_compile_features(bds_block_slowdown_selftest PRIVATE cxx_std_23)
    add_test(NAME bds_block_slowdown_selftest COMMAND bds_block_slowdown_selftest
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/bds_block_slowdown.txt")

    add_executable(bds_sneak_selftest tests/bds_sneak_selftest.cpp)
    target_include_directories(bds_sneak_selftest PRIVATE src)
    target_compile_features(bds_sneak_selftest PRIVATE cxx_std_23)
    add_test(NAME bds_sneak_selftest COMMAND bds_sneak_selftest
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/bds_sneak.txt")

    add_executable(bds_inside_query_selftest tests/bds_inside_query_selftest.cpp)
    target_include_directories(bds_inside_query_selftest PRIVATE src)
    target_compile_features(bds_inside_query_selftest PRIVATE cxx_std_23)
    add_test(NAME bds_inside_query_selftest COMMAND bds_inside_query_selftest
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/bds_inside_query.txt")

    add_executable(bds_jump_selftest tests/bds_jump_selftest.cpp)
    target_include_directories(bds_jump_selftest PRIVATE src)
    target_compile_features(bds_jump_selftest PRIVATE cxx_std_23)
    add_test(NAME bds_jump_selftest COMMAND bds_jump_selftest
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/bds_jump.txt")

    add_executable(bds_jump_requests_selftest tests/bds_jump_requests_selftest.cpp)
    target_include_directories(bds_jump_requests_selftest PRIVATE src)
    target_compile_features(bds_jump_requests_selftest PRIVATE cxx_std_23)
    add_test(NAME bds_jump_requests_selftest COMMAND bds_jump_requests_selftest
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/bds_jump_requests.txt")

    add_executable(bds_jump_predicate_selftest tests/bds_jump_predicate_selftest.cpp)
    target_include_directories(bds_jump_predicate_selftest PRIVATE src)
    target_compile_features(bds_jump_predicate_selftest PRIVATE cxx_std_23)
    add_test(NAME bds_jump_predicate_selftest COMMAND bds_jump_predicate_selftest
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/bds_jump_predicate.txt")

    add_executable(bds_honey_selftest tests/bds_honey_selftest.cpp)
    target_include_directories(bds_honey_selftest PRIVATE src)
    target_compile_features(bds_honey_selftest PRIVATE cxx_std_23)
    add_test(NAME bds_honey_selftest COMMAND bds_honey_selftest
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/bds_honey.txt")

    add_executable(bds_sprint_trace_selftest tests/bds_sprint_trace_selftest.cpp)
    target_include_directories(bds_sprint_trace_selftest PRIVATE src)
    target_compile_features(bds_sprint_trace_selftest PRIVATE cxx_std_23)
    add_test(NAME bds_sprint_trace_selftest COMMAND bds_sprint_trace_selftest
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/bedsim_sprint_jumps.txt")

    add_executable(bds_fluid_state_selftest tests/bds_fluid_state_selftest.cpp)
    target_include_directories(bds_fluid_state_selftest PRIVATE src)
    target_compile_features(bds_fluid_state_selftest PRIVATE cxx_std_23)
    add_test(NAME bds_fluid_state_selftest COMMAND bds_fluid_state_selftest
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/bds_fluid_state.txt")

    add_executable(bds_liquid_cells_selftest tests/bds_liquid_cells_selftest.cpp)
    target_include_directories(bds_liquid_cells_selftest PRIVATE src)
    target_compile_features(bds_liquid_cells_selftest PRIVATE cxx_std_23)
    add_test(NAME bds_liquid_cells_selftest COMMAND bds_liquid_cells_selftest
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/bds_liquid_cells.txt")

    add_executable(bedsim_trace_selftest
        tests/bedsim_trace_selftest.cpp
        src/client/player/PlayerPhysics.cpp)
    target_include_directories(bedsim_trace_selftest PRIVATE src)
    target_compile_features(bedsim_trace_selftest PRIVATE cxx_std_23)
    add_test(NAME bedsim_trace_selftest COMMAND bedsim_trace_selftest "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/bedsim_traces.txt")

    add_executable(panorama_selftest tests/panorama_selftest.cpp src/client/ui/TitlePanorama.cpp)
    target_include_directories(panorama_selftest PRIVATE src)
    target_compile_features(panorama_selftest PRIVATE cxx_std_23)
    add_test(NAME panorama_selftest COMMAND panorama_selftest)

    add_executable(uitemplate_selftest tests/uitemplate_selftest.cpp src/client/ui/UiTemplate.cpp src/util/Paths.cpp)
    target_include_directories(uitemplate_selftest PRIVATE src)
    target_compile_features(uitemplate_selftest PRIVATE cxx_std_23)
    add_test(NAME uitemplate_selftest COMMAND uitemplate_selftest)

    add_executable(trace_diff
        tests/trace_diff.cpp
        src/client/player/PlayerPhysics.cpp)
    target_include_directories(trace_diff PRIVATE src)
    target_compile_features(trace_diff PRIVATE cxx_std_23)

    add_executable(damage_selftest tests/damage_selftest.cpp)
    target_include_directories(damage_selftest PRIVATE src "${glm_SOURCE_DIR}")
    target_compile_features(damage_selftest PRIVATE cxx_std_23)
    add_test(NAME damage_selftest COMMAND damage_selftest)

    add_executable(models_selftest tests/models_selftest.cpp src/client/renderer/block/BlockModels.cpp)
    target_include_directories(models_selftest PRIVATE src)
    target_compile_features(models_selftest PRIVATE cxx_std_23)
    add_test(NAME models_selftest COMMAND models_selftest "${CMAKE_CURRENT_SOURCE_DIR}/resources/block_models.bin")

    file(GLOB TEXTURE_TEST_SOURCES CONFIGURE_DEPENDS
        src/client/renderer/block/*.cpp src/client/resources/TextureLoader.cpp src/client/resources/TexturePack.cpp
        src/client/resources/JavaPack.cpp)
    add_executable(texture_selftest
        tests/texture_selftest.cpp ${TEXTURE_TEST_SOURCES}
        src/client/Options.cpp src/client/input/KeyMapping.cpp
        src/util/Json.cpp src/util/Paths.cpp
        src/world/level/block/BlockPalette.cpp src/util/Resources.cpp
        src/client/resources/ServerPackRegistry.cpp src/client/resources/ServerBlocks.cpp src/client/ui/Json.cpp)
    target_include_directories(texture_selftest PRIVATE src "${stb_SOURCE_DIR}")
    target_link_libraries(texture_selftest PRIVATE glaze::glaze miniz Threads::Threads)
    target_compile_features(texture_selftest PRIVATE cxx_std_23)
    if(PERLIN_TEXTURE_TEST_ASSETS)
        add_test(NAME texture_selftest COMMAND texture_selftest "${PERLIN_TEXTURE_TEST_ASSETS}")
    endif()

    add_executable(javapack_selftest
        tests/javapack_selftest.cpp src/client/resources/JavaPack.cpp src/client/resources/TexturePack.cpp
        src/client/resources/TextureLoader.cpp src/util/Json.cpp)
    target_include_directories(javapack_selftest PRIVATE src "${stb_SOURCE_DIR}")
    target_link_libraries(javapack_selftest PRIVATE glaze::glaze miniz Threads::Threads)
    target_compile_features(javapack_selftest PRIVATE cxx_std_23)
    add_test(NAME javapack_selftest COMMAND javapack_selftest)

    add_executable(auth_selftest
        tests/auth_selftest.cpp
        src/network/XboxAuth.cpp src/network/LoginBuilder.cpp src/network/ClientIdentity.cpp src/network/Http.cpp src/network/PacketCipher.cpp
        src/client/multiplayer/SkinLoader.cpp src/client/resources/TextureLoader.cpp src/util/Json.cpp)
    target_include_directories(auth_selftest PRIVATE src "${stb_SOURCE_DIR}" "${httplib_SOURCE_DIR}")
    target_compile_definitions(auth_selftest PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT OPENSSL_SUPPRESS_DEPRECATED)
    target_link_libraries(auth_selftest PRIVATE glaze::glaze miniz OpenSSL::SSL OpenSSL::Crypto Threads::Threads)
    if(WIN32)
        target_link_libraries(auth_selftest PRIVATE ws2_32 crypt32)
    elseif(APPLE)
        target_link_libraries(auth_selftest PRIVATE "-framework Security" "-framework CoreFoundation")
        target_compile_definitions(auth_selftest PRIVATE CPPHTTPLIB_USE_CERTS_FROM_MACOSX_KEYCHAIN)
    endif()
    target_compile_features(auth_selftest PRIVATE cxx_std_23)
    add_test(NAME auth_selftest COMMAND auth_selftest)

    add_executable(rak_selftest tests/rak_selftest.cpp src/network/RakNet.cpp)
    add_executable(client_control_selftest tests/client_control_selftest.cpp
        src/client/ClientControl.cpp src/client/input/Input.cpp src/client/input/InputManager.cpp
        src/client/input/KeyMapping.cpp src/client/input/Keys.cpp src/client/Options.cpp src/util/Json.cpp src/util/Paths.cpp)
    target_include_directories(client_control_selftest PRIVATE src "${httplib_SOURCE_DIR}" "${stb_SOURCE_DIR}"
        "${concurrentqueue_SOURCE_DIR}" "${glm_SOURCE_DIR}")
    target_link_libraries(client_control_selftest PRIVATE glaze::glaze Threads::Threads)
    target_compile_features(client_control_selftest PRIVATE cxx_std_23)
    if(WIN32)
        target_link_libraries(client_control_selftest PRIVATE ws2_32 crypt32)
    endif()
    add_test(NAME client_control_selftest COMMAND client_control_selftest)
    target_include_directories(rak_selftest PRIVATE src "${concurrentqueue_SOURCE_DIR}")
    target_link_libraries(rak_selftest PRIVATE Threads::Threads)
    if(WIN32)
        target_link_libraries(rak_selftest PRIVATE ws2_32)
    endif()
    target_compile_features(rak_selftest PRIVATE cxx_std_23)

    add_executable(clientassets_selftest tests/clientassets_selftest.cpp src/client/resources/ClientAssets.cpp src/client/resources/TextureLoader.cpp src/util/Json.cpp)
    target_include_directories(clientassets_selftest PRIVATE src "${stb_SOURCE_DIR}")
    target_link_libraries(clientassets_selftest PRIVATE glaze::glaze miniz)
    target_compile_features(clientassets_selftest PRIVATE cxx_std_23)

    add_executable(shaderpack_selftest tests/shaderpack_selftest.cpp src/client/resources/ShaderPack.cpp src/client/ui/Json.cpp tests/BundledShaderStub.cpp)
    target_include_directories(shaderpack_selftest PRIVATE src)
    target_link_libraries(shaderpack_selftest PRIVATE miniz)
    target_compile_features(shaderpack_selftest PRIVATE cxx_std_23)
    add_test(NAME shaderpack_selftest COMMAND shaderpack_selftest)

    add_executable(light_selftest
        tests/light_selftest.cpp
        src/world/level/lighting/ChunkLighting.cpp
        src/world/level/lighting/LightRegistry.cpp
        src/util/Json.cpp
        src/world/level/Level.cpp
        src/world/level/block/BlockPalette.cpp src/util/Resources.cpp src/util/Paths.cpp)
    target_include_directories(light_selftest PRIVATE src)
    target_link_libraries(light_selftest PRIVATE glaze::glaze Threads::Threads)
    target_compile_features(light_selftest PRIVATE cxx_std_23)
    add_test(NAME light_selftest COMMAND light_selftest)

    add_executable(mod_selftest tests/mod_selftest.cpp src/client/mods/ModManager.cpp src/client/mods/WasmRuntime.cpp src/client/ui/Json.cpp)
    target_include_directories(mod_selftest PRIVATE src mods/sdk)
    target_link_libraries(mod_selftest PRIVATE perlin_wasmedge Threads::Threads)
    target_compile_features(mod_selftest PRIVATE cxx_std_23)
    set(PERLIN_TEST_PLUGINS "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/plugins")
    add_test(NAME mod_selftest COMMAND mod_selftest
        "${PERLIN_TEST_PLUGINS}/hello64.wasm" "${PERLIN_TEST_PLUGINS}/hello32.wasm"
        "${PERLIN_TEST_PLUGINS}/loop_64.wasm" "${PERLIN_TEST_PLUGINS}/loop_32.wasm"
        "${CMAKE_CURRENT_SOURCE_DIR}/mods/prebuilt/waila.wasm")

    find_package(Python3 QUIET COMPONENTS Interpreter)
    if(Python3_FOUND)
        add_test(NAME mod_api_check COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/mods/tools/check_api.py")
        if(NOT MSVC)
            set(PERLIN_MODS_GEN "${CMAKE_CURRENT_BINARY_DIR}/mods_gen")
            foreach(spec "waila:mods/waila/waila.json" "sdk:mods/tests/data/settings_test.json")
                string(REPLACE ":" ";" spec_parts "${spec}")
                list(GET spec_parts 0 gen_name)
                list(GET spec_parts 1 gen_manifest)
                add_custom_command(OUTPUT "${PERLIN_MODS_GEN}/${gen_name}/mod_settings.hpp"
                    COMMAND ${CMAKE_COMMAND} -E make_directory "${PERLIN_MODS_GEN}/${gen_name}"
                    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/mods/tools/perlin_settings.py"
                            "${CMAKE_CURRENT_SOURCE_DIR}/${gen_manifest}" "${PERLIN_MODS_GEN}/${gen_name}/mod_settings.hpp"
                    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${gen_manifest}" "${CMAKE_CURRENT_SOURCE_DIR}/mods/tools/perlin_settings.py")
            endforeach()
            add_executable(waila_test mods/waila/tests/waila_test.cpp "${PERLIN_MODS_GEN}/waila/mod_settings.hpp")
            target_include_directories(waila_test PRIVATE mods/sdk mods/waila "${PERLIN_MODS_GEN}/waila")
            target_compile_definitions(waila_test PRIVATE MANIFEST="${CMAKE_CURRENT_SOURCE_DIR}/mods/waila/waila.json")
            target_compile_features(waila_test PRIVATE cxx_std_23)
            add_test(NAME waila_test COMMAND waila_test)
            add_executable(sdk_test mods/tests/sdk_test.cpp "${PERLIN_MODS_GEN}/sdk/mod_settings.hpp")
            target_include_directories(sdk_test PRIVATE mods/sdk "${PERLIN_MODS_GEN}/sdk")
            target_compile_features(sdk_test PRIVATE cxx_std_23)
            add_test(NAME sdk_test COMMAND sdk_test)
            foreach(wire_test abi_test meta_test plugin_test)
                add_executable(${wire_test} mods/tests/${wire_test}.cpp)
                target_include_directories(${wire_test} PRIVATE mods/sdk)
                target_compile_features(${wire_test} PRIVATE cxx_std_23)
                add_test(NAME ${wire_test} COMMAND ${wire_test})
            endforeach()
            if(APPLE)
                foreach(native_test waila_test sdk_test abi_test meta_test plugin_test)
                    target_compile_definitions(${native_test} PRIVATE _LIBCPP_DISABLE_AVAILABILITY)
                endforeach()
            endif()
        endif()
    endif()
endif()

# The table is consumed by header-only physics kernels and their tests.
# Keep this include path off external dependency targets.
get_property(PERLIN_LOCAL_TARGETS DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(PERLIN_LOCAL_TARGET IN LISTS PERLIN_LOCAL_TARGETS)
    if(PERLIN_LOCAL_TARGET MATCHES "^(Perlin|trace_diff)$|selftest$")
        target_include_directories(${PERLIN_LOCAL_TARGET} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
    endif()
endforeach()

# perlin server: dedicated exe, shared library for singleplayer, plugins; see server/README.md
# on by default: the title screen's Singleplayer button loads perlin_server from beside the executable.
# not built for the web or android (no processes or shared libraries to load there); the button then says so
option(PERLIN_BUILD_SERVER "Build the Perlin server and singleplayer (dedicated executable and perlin_server library)" ON)
if(PERLIN_BUILD_SERVER AND NOT EMSCRIPTEN AND NOT ANDROID)
    add_subdirectory(server)
    # singleplayer: the client loads perlin_server beside the executable and joins it in process
    if(TARGET Perlin AND TARGET perlin_server)
        target_sources(Perlin PRIVATE server/client_bridge/EmbeddedServer.cpp)
        target_include_directories(Perlin PRIVATE server/client_bridge server/include)
        target_compile_definitions(Perlin PRIVATE PERLIN_SINGLEPLAYER=1 PERLIN_SERVER_STATIC)
        add_dependencies(Perlin perlin_server)
        target_link_libraries(Perlin PRIVATE ${CMAKE_DL_LIBS})
        # next to the executable in the package (on macOS the copy above already sits inside the app bundle)
        if(NOT APPLE)
            install(TARGETS perlin_server RUNTIME DESTINATION . LIBRARY DESTINATION .)
        endif()
    endif()
    # the dedicated server ships beside the client with the block tables it reads
    if(TARGET perlin-server)
        install(TARGETS perlin-server RUNTIME DESTINATION .)
        install(FILES resources/block_states.json resources/block_destroy.tsv resources/extra_items.txt DESTINATION resources)
    endif()
endif()
