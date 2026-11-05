# perlin server: dedicated executable, shared library (dll / so / dylib) and static core to embed in the client
# standalone:  cmake -S server -B build-server && cmake --build build-server
# in the tree: the root CMakeLists adds this directory when PERLIN_BUILD_SERVER is ON and links perlin_server_core into the client
cmake_minimum_required(VERSION 3.28)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(perlin_server LANGUAGES C CXX)
    set(PERLIN_SERVER_STANDALONE ON)
    set(CMAKE_CXX_STANDARD 23)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_BUILD_TYPE Release)
    endif()
else()
    set(PERLIN_SERVER_STANDALONE OFF)
endif()

option(PERLIN_SERVER_NETHERNET "NetherNet listener (libdatachannel + http signalling); without it only loopback connections work" ON)
option(PERLIN_SERVER_GPU "GPU terrain generation through conduit" OFF)
option(PERLIN_SERVER_EMBED "also build perlin_server_embed: the core without a wasm runtime, linked into the client (which brings its own)" OFF)
option(PERLIN_SERVER_BUILD_SHARED "build the perlin_server shared library" ON)
option(PERLIN_SERVER_BUILD_EXE "build the perlin-server executable" ON)
option(PERLIN_SERVER_BUILD_TESTS "build the server tests" ${PERLIN_SERVER_STANDALONE})

get_filename_component(PERLIN_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
include(FetchContent)
find_package(Threads REQUIRED)

# protocol codecs: the client's generated packets, so the server speaks exactly what the client decodes
if(NOT TARGET perlin_protocol)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(PROTO_OUT "${CMAKE_CURRENT_BINARY_DIR}/generated/protocol")
    set(PROTO_SOURCES)
    foreach(i RANGE 0 5)
        list(APPEND PROTO_SOURCES "${PROTO_OUT}/Codec${i}.cpp")
    endforeach()
    set(PROTO_HEADERS "${PROTO_OUT}/Types.hpp" "${PROTO_OUT}/Packets.hpp")
    add_custom_command(
        OUTPUT ${PROTO_HEADERS} ${PROTO_SOURCES} "${PROTO_OUT}/RoundTrip.cpp" "${PROTO_OUT}/PacketJson.cpp"
        COMMAND "${Python3_EXECUTABLE}" -I "${PERLIN_ROOT}/tools/protogen/protogen.py"
                --spec "${PERLIN_ROOT}/protocol/bedrock-1.26.60.json"
                --overrides "${PERLIN_ROOT}/protocol/overrides.json"
                --out "${PROTO_OUT}" --parts 6 --namespace proto2225
        DEPENDS "${PERLIN_ROOT}/tools/protogen/protogen.py" "${PERLIN_ROOT}/protocol/bedrock-1.26.60.json" "${PERLIN_ROOT}/protocol/overrides.json"
        COMMENT "Generating protocol codecs"
        VERBATIM)
    add_library(perlin_protocol STATIC "${PERLIN_ROOT}/src/network/Nbt.cpp" ${PROTO_SOURCES} ${PROTO_HEADERS})
    target_include_directories(perlin_protocol PUBLIC "${PERLIN_ROOT}/src" "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_compile_features(perlin_protocol PUBLIC cxx_std_23)
    set_source_files_properties(${PROTO_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
endif()

if(NOT TARGET miniz)
    FetchContent_Declare(miniz GIT_REPOSITORY https://github.com/richgel999/miniz.git GIT_TAG 3.0.2 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(miniz)
endif()

# the client builds these as plain static libraries; they go into a shared library here
foreach(dep perlin_protocol miniz)
    if(TARGET ${dep})
        set_target_properties(${dep} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endif()
endforeach()

set(PS_SOURCES
    src/core/Access.cpp
    src/core/Commands.cpp
    src/core/CommandsBuiltin.cpp
    src/core/Auth.cpp
    src/core/CApi.cpp
    src/core/Config.cpp
    src/core/Json.cpp
    src/core/Log.cpp
    src/core/Server.cpp
    src/inventory/Inventory.cpp
    src/net/Batch.cpp
    src/net/Connection.cpp
    src/net/LoopbackListener.cpp
    src/plugin/PluginHost.cpp
    src/session/Session.cpp
    src/world/Backends.cpp
    src/world/BlockCatalog.cpp
    src/world/Blocks.cpp
    src/world/Chunk.cpp
    src/world/ChunkManager.cpp
    src/world/Mobs.cpp
    src/core/ServerContainers.cpp
    src/core/ServerPortals.cpp
    src/core/CommandExecute.cpp
    src/inventory/Recipes.cpp
    src/inventory/RecipeData.cpp
    src/world/MobData.cpp
    src/world/Generator.cpp
    src/world/vanilla/DataPack.cpp
    src/world/vanilla/DensityFunction.cpp
    src/world/vanilla/Noise.cpp
    src/world/vanilla/VanillaTerrain.cpp
    src/world/vanilla/VanillaDimension.cpp
    src/world/vanilla/OverworldShapeData.cpp
    src/world/vanilla/BiomeData.cpp
    src/world/vanilla/VanillaSurface.cpp
    src/world/vanilla/VanillaAquifer.cpp
    src/world/vanilla/VanillaMaterials.cpp
    src/world/vanilla/VanillaCarvers.cpp src/world/vanilla/VanillaDimension.cpp
    src/world/vanilla/VanillaFeatures.cpp
    src/world/vanilla/FeatureData.cpp
    src/world/vanilla/JavaDecorator.cpp
    src/world/vanilla/JavaFeatureData.cpp
    src/gpu/VanillaGpuProgram.cpp)

# wasm runtime: wasm3 through the client's own wrapper (WasmRuntimeWasm3.cpp), so plugins behave the same.
# it is plain c with no dependencies, which keeps it safe to link into a shared library next to the client's WasmEdge
if(TRUE)
    if(NOT TARGET wasm3_core)
        FetchContent_Declare(wasm3 GIT_REPOSITORY https://github.com/wasm3/wasm3.git GIT_TAG 28ecb9af6d2040e474a70f7cb7f43666740141fb)
        FetchContent_GetProperties(wasm3)
        if(NOT wasm3_POPULATED)
            FetchContent_Populate(wasm3)
        endif()
        file(GLOB WASM3_CORE_SOURCES "${wasm3_SOURCE_DIR}/source/*.c")
        list(FILTER WASM3_CORE_SOURCES EXCLUDE REGEX "m3_api_")
        add_library(wasm3_core STATIC ${WASM3_CORE_SOURCES})
        target_include_directories(wasm3_core PUBLIC "${wasm3_SOURCE_DIR}/source")
        target_compile_definitions(wasm3_core PRIVATE d_m3HasTracer=0)
        if(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
            target_compile_definitions(wasm3_core PRIVATE MAP_NORESERVE=0)
        endif()
        target_compile_options(wasm3_core PRIVATE -w)
        set_target_properties(wasm3_core PROPERTIES POSITION_INDEPENDENT_CODE ON C_STANDARD 11)
    endif()
    set(PS_WASM_SOURCES "${PERLIN_ROOT}/src/client/mods/WasmRuntimeWasm3.cpp")
    set(PS_WASM_LIB wasm3_core)
endif()

if(PERLIN_SERVER_NETHERNET)
    list(APPEND PS_SOURCES src/net/NetherNetListener.cpp)
    if(NOT TARGET datachannel)
        set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
        set(NO_TESTS ON CACHE BOOL "" FORCE)
        FetchContent_Declare(libdatachannel GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git GIT_TAG v0.23.2 GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(libdatachannel)
    endif()
    if(NOT httplib_SOURCE_DIR)
        FetchContent_Declare(httplib GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git GIT_TAG v0.18.7 GIT_SHALLOW TRUE)
        FetchContent_GetProperties(httplib)
        if(NOT httplib_POPULATED)
            FetchContent_Populate(httplib)
        endif()
    endif()
else()
    list(APPEND PS_SOURCES src/net/NetherNetStub.cpp)
endif()

if(PERLIN_SERVER_GPU)
    list(APPEND PS_SOURCES src/gpu/GpuTerrainRhi.cpp src/gpu/GpuVanillaRhi.cpp "${CMAKE_CURRENT_BINARY_DIR}/gpu/terrain_pack.cpp")
    if(NOT TARGET conduit::conduit)
        set(CONDUIT_D3D12 OFF CACHE BOOL "" FORCE)
        set(CONDUIT_STREAMLINE OFF CACHE BOOL "" FORCE)
        include("${PERLIN_ROOT}/cmake/PerlinConduit.cmake")
    endif()
    # compute shader -> shader pack (spir-v for now) -> a byte array in the library
    set(PERLIN_SLANGC "" CACHE FILEPATH "slangc to compile the terrain shader with; fetched when empty")
    if(NOT PERLIN_SLANGC)
        set(_slang_version 2026.20)
        if(CMAKE_HOST_WIN32)
            set(_slang_os windows)
            set(_slang_ext zip)
        elseif(CMAKE_HOST_APPLE)
            set(_slang_os macos)
            set(_slang_ext zip)
        else()
            set(_slang_os linux)
            set(_slang_ext tar.gz)
        endif()
        if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64|ARM64)$")
            set(_slang_arch aarch64)
        else()
            set(_slang_arch x86_64)
        endif()
        FetchContent_Declare(slang URL "https://github.com/shader-slang/slang/releases/download/v${_slang_version}/slang-${_slang_version}-${_slang_os}-${_slang_arch}.${_slang_ext}")
        FetchContent_MakeAvailable(slang)
        set(PERLIN_SLANGC "${slang_SOURCE_DIR}/bin/slangc${CMAKE_EXECUTABLE_SUFFIX}")
    endif()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/gpu/slots.json" "{}")
    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/gpu/terrain.pack" "${CMAKE_CURRENT_BINARY_DIR}/gpu/terrain_pack.cpp"
        COMMAND "${Python3_EXECUTABLE}" -I "${CONDUIT_PACK_SHADERS}" --slangc "${PERLIN_SLANGC}"
                --slots "${CMAKE_CURRENT_BINARY_DIR}/gpu/slots.json" --targets spirv
                -o "${CMAKE_CURRENT_BINARY_DIR}/gpu/terrain.pack" "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/shaders/terrain_flat.slang"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/shaders/terrain_vanilla.slang"
        COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_bytes.py" "${CMAKE_CURRENT_BINARY_DIR}/gpu/terrain.pack"
                "${CMAKE_CURRENT_BINARY_DIR}/gpu/terrain_pack.cpp" kTerrainShaderPack
        DEPENDS src/gpu/shaders/terrain_flat.slang src/gpu/shaders/terrain_vanilla.slang tools/embed_bytes.py "${CONDUIT_PACK_SHADERS}"
        COMMENT "Compiling the terrain compute shader" VERBATIM)
else()
    list(APPEND PS_SOURCES src/gpu/GpuTerrainStub.cpp)
endif()

find_package(OpenSSL REQUIRED)
# every server source is compiled once, into perlin_server_objects; the libraries below only differ in the wasm runtime
add_library(perlin_server_objects OBJECT ${PS_SOURCES})
set(PS_LIBS perlin_server_core)
add_library(perlin_server_core STATIC $<TARGET_OBJECTS:perlin_server_objects> ${PS_WASM_SOURCES})
target_link_libraries(perlin_server_core PRIVATE ${PS_WASM_LIB})
if(PERLIN_SERVER_EMBED)
    # singleplayer inside the client: the client already links a wasm runtime (WasmEdge or wasm3) behind the same api
    add_library(perlin_server_embed STATIC $<TARGET_OBJECTS:perlin_server_objects>)
    list(APPEND PS_LIBS perlin_server_embed)
endif()
foreach(t perlin_server_objects ${PS_LIBS})
    target_include_directories(${t} PUBLIC "${PERLIN_ROOT}/mods/sdk" include PRIVATE src "${PERLIN_ROOT}/src")
    target_compile_features(${t} PUBLIC cxx_std_23)
    set_target_properties(${t} PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
    target_compile_definitions(${t} PRIVATE PERLIN_SERVER_BUILD_SHARED "PERLIN_SERVER_RESOURCE_DIR=\"${PERLIN_ROOT}/resources\"")
    target_link_libraries(${t} PUBLIC perlin_protocol miniz OpenSSL::Crypto Threads::Threads)
    if(PERLIN_SERVER_NETHERNET)
        # the client builds libdatachannel static (position independent) and shared; the static one keeps the library self contained
        if(TARGET datachannel-static)
            target_link_libraries(${t} PUBLIC datachannel-static OpenSSL::SSL)
        else()
            target_link_libraries(${t} PUBLIC datachannel)
        endif()
        target_include_directories(${t} PRIVATE "${httplib_SOURCE_DIR}")
        target_compile_definitions(${t} PRIVATE CPPHTTPLIB_THREAD_POOL_COUNT=16)
    endif()
    if(PERLIN_SERVER_GPU)
        target_link_libraries(${t} PUBLIC conduit::conduit)
        target_compile_definitions(${t} PRIVATE PERLIN_SERVER_GPU=1)
    endif()
    if(WIN32)
        target_compile_definitions(${t} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
        target_link_libraries(${t} PUBLIC ws2_32 crypt32 bcrypt iphlpapi)
    endif()
    if(MSVC)
        target_compile_options(${t} PRIVATE /W4 /utf-8)
    else()
        target_compile_options(${t} PRIVATE -Wall -Wextra -Wno-unused-parameter)
    endif()
endforeach()
# float results must match java's strict ieee arithmetic bit for bit: no fused multiply-add
if(NOT MSVC)
    set_source_files_properties(src/world/vanilla/Noise.cpp src/world/vanilla/DensityFunction.cpp src/world/vanilla/VanillaTerrain.cpp src/world/vanilla/VanillaSurface.cpp src/world/vanilla/VanillaAquifer.cpp src/world/vanilla/VanillaMaterials.cpp src/world/vanilla/VanillaCarvers.cpp src/world/vanilla/VanillaDimension.cpp
    src/gpu/VanillaGpuProgram.cpp PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")
endif()

if(PERLIN_SERVER_BUILD_SHARED)
    add_library(perlin_server SHARED src/core/ShimExports.cpp)
    target_link_libraries(perlin_server PRIVATE perlin_server_core)
    target_compile_definitions(perlin_server PRIVATE PERLIN_SERVER_BUILD_SHARED)
    set_target_properties(perlin_server PROPERTIES CXX_VISIBILITY_PRESET hidden)
    # the exported c api lives in the static core: keep every object of it in the library
    if(MSVC)
        target_link_options(perlin_server PRIVATE "/WHOLEARCHIVE:$<TARGET_FILE:perlin_server_core>")
    elseif(APPLE)
        target_link_options(perlin_server PRIVATE "LINKER:-force_load,$<TARGET_FILE:perlin_server_core>")
    else()
        target_link_options(perlin_server PRIVATE -Wl,--whole-archive "$<TARGET_FILE:perlin_server_core>" -Wl,--no-whole-archive)
    endif()
    add_dependencies(perlin_server perlin_server_core)
endif()

# release builds on Linux carry no symbol tables
if(UNIX AND NOT APPLE)
    foreach(t perlin_server perlin-server)
        if(TARGET ${t})
            target_link_options(${t} PRIVATE $<$<CONFIG:Release>:-s>)
        endif()
    endforeach()
endif()

if(PERLIN_SERVER_BUILD_EXE)
    add_executable(perlin-server tools/main.cpp)
    target_include_directories(perlin-server PRIVATE src)
    target_link_libraries(perlin-server PRIVATE perlin_server_core Threads::Threads)
endif()

if(PERLIN_SERVER_BUILD_TESTS)
    enable_testing()
    # the client's chunk decoder checks what the server encodes
    set(PS_CLIENT_CHUNK_CODEC "${PERLIN_ROOT}/src/world/level/chunk/ChunkCodec.cpp")

    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/hello_server.wasm"
        COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/examples/hello_server/build.sh" "${CMAKE_CURRENT_BINARY_DIR}/hello_server.wasm"
        DEPENDS examples/hello_server/hello_server.c ${PERLIN_ROOT}/mods/sdk/perlin.h ${PERLIN_ROOT}/mods/sdk/perlin/abi_table.h examples/hello_server/build.sh
        COMMENT "Building the example plugin" VERBATIM)
    add_custom_target(server_example_plugin DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/hello_server.wasm")

    # the java terrain port: checked number for number against the real engine's output (tools/javaref)
    add_executable(vanilla_noise_test tests/vanilla_noise_test.cpp)
    target_include_directories(vanilla_noise_test PRIVATE src)
    target_link_libraries(vanilla_noise_test PRIVATE perlin_server_core miniz OpenSSL::Crypto Threads::Threads)
    add_test(NAME vanilla_noise_test COMMAND vanilla_noise_test)
    add_executable(vanilla_terrain_test tests/vanilla_terrain_test.cpp)
    target_include_directories(vanilla_terrain_test PRIVATE src)
    target_link_libraries(vanilla_terrain_test PRIVATE perlin_server_core miniz OpenSSL::Crypto Threads::Threads)
    add_test(NAME vanilla_terrain_test COMMAND vanilla_terrain_test)
    add_executable(vanilla_terrain_bench tests/vanilla_terrain_bench.cpp)
    target_include_directories(vanilla_terrain_bench PRIVATE src)
    target_link_libraries(vanilla_terrain_bench PRIVATE perlin_server_core miniz OpenSSL::Crypto Threads::Threads)
    add_executable(vanilla_chunk_dump tests/vanilla_chunk_dump.cpp)
    target_include_directories(vanilla_chunk_dump PRIVATE src)
    target_link_libraries(vanilla_chunk_dump PRIVATE perlin_server_core miniz OpenSSL::Crypto Threads::Threads)
    add_executable(vanilla_density_test tests/vanilla_density_test.cpp)
    target_include_directories(vanilla_density_test PRIVATE src)
    target_link_libraries(vanilla_density_test PRIVATE perlin_server_core miniz OpenSSL::Crypto Threads::Threads)
    add_test(NAME vanilla_density_test COMMAND vanilla_density_test "${PERLIN_ROOT}/tools/javaref/reference_seed12345.txt")
    add_executable(vanilla_biome_test tests/vanilla_biome_test.cpp)
    target_include_directories(vanilla_biome_test PRIVATE src)
    target_link_libraries(vanilla_biome_test PRIVATE perlin_server_core miniz OpenSSL::Crypto Threads::Threads)
    add_test(NAME vanilla_biome_test COMMAND vanilla_biome_test "${PERLIN_ROOT}/tools/javaref/biome_lookup_ref.txt")
    add_executable(vanilla_decoration_test tests/vanilla_decoration_test.cpp)
    target_include_directories(vanilla_decoration_test PRIVATE src)
    target_link_libraries(vanilla_decoration_test PRIVATE perlin_server_core miniz OpenSSL::Crypto Threads::Threads)
    add_test(NAME vanilla_decoration_test COMMAND vanilla_decoration_test "${PERLIN_ROOT}/resources")

    add_executable(vanilla_features_test tests/vanilla_features_test.cpp)
    target_include_directories(vanilla_features_test PRIVATE src)
    target_link_libraries(vanilla_features_test PRIVATE perlin_server_core miniz OpenSSL::Crypto Threads::Threads)
    add_test(NAME vanilla_features_test COMMAND vanilla_features_test)
    add_executable(vanilla_gpu_test tests/vanilla_gpu_test.cpp)
    target_include_directories(vanilla_gpu_test PRIVATE src "${PERLIN_ROOT}/src")
    target_link_libraries(vanilla_gpu_test PRIVATE perlin_server_core miniz OpenSSL::Crypto Threads::Threads)
    add_test(NAME vanilla_gpu_test COMMAND vanilla_gpu_test)

    add_executable(server_selftest tests/server_selftest.cpp ${PS_CLIENT_CHUNK_CODEC})
    target_include_directories(server_selftest PRIVATE src "${PERLIN_ROOT}/src")
    target_link_libraries(server_selftest PRIVATE perlin_server_core miniz OpenSSL::Crypto Threads::Threads)
    add_dependencies(server_selftest server_example_plugin)
    add_test(NAME server_selftest COMMAND server_selftest "${CMAKE_CURRENT_BINARY_DIR}/hello_server.wasm")

    add_executable(server_session_test tests/server_session_test.cpp ${PS_CLIENT_CHUNK_CODEC})
    target_include_directories(server_session_test PRIVATE src "${PERLIN_ROOT}/src")
    target_link_libraries(server_session_test PRIVATE perlin_server_core miniz OpenSSL::Crypto Threads::Threads)
    add_dependencies(server_session_test server_example_plugin)
    add_test(NAME server_session_test COMMAND server_session_test "${CMAKE_CURRENT_BINARY_DIR}/hello_server.wasm")

    # the client-side bridge, built from the same source the client compiles, against the linked-in server
    add_executable(server_embed_test tests/server_embed_test.cpp client_bridge/EmbeddedServer.cpp)
    target_include_directories(server_embed_test PRIVATE src client_bridge include "${PERLIN_ROOT}/src")
    target_compile_definitions(server_embed_test PRIVATE PERLIN_SERVER_STATIC)
    target_link_libraries(server_embed_test PRIVATE perlin_server_core Threads::Threads)
    add_test(NAME server_embed_test COMMAND server_embed_test)

    if(PERLIN_SERVER_GPU)
        add_executable(server_gpu_test tests/server_gpu_test.cpp)
        target_include_directories(server_gpu_test PRIVATE src "${PERLIN_ROOT}/src")
        target_link_libraries(server_gpu_test PRIVATE perlin_server_core miniz OpenSSL::Crypto Threads::Threads)
        add_test(NAME server_gpu_test COMMAND server_gpu_test)
    endif()

    add_executable(server_capi_test tests/server_capi_test.cpp)
    target_include_directories(server_capi_test PRIVATE src "${PERLIN_ROOT}/src")
    target_link_libraries(server_capi_test PRIVATE perlin_server_core miniz OpenSSL::Crypto Threads::Threads)
    add_test(NAME server_capi_test COMMAND server_capi_test)

    if(PERLIN_SERVER_NETHERNET)
        add_executable(server_nethernet_test tests/server_nethernet_test.cpp)
        target_include_directories(server_nethernet_test PRIVATE src "${PERLIN_ROOT}/src" "${httplib_SOURCE_DIR}")
        target_link_libraries(server_nethernet_test PRIVATE perlin_server_core datachannel miniz OpenSSL::Crypto Threads::Threads)
        add_test(NAME server_nethernet_test COMMAND server_nethernet_test)
    endif()
endif()
