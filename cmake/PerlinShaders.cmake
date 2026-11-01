# Slang shaders: every program is compiled for the graphics apis enabled for this platform and packed into one file that the engine embeds.
set(PERLIN_SLANG_VERSION "2026.20" CACHE STRING "Slang release used to compile the shaders")
set(PERLIN_DXC_RELEASE "v1.8.2502" CACHE STRING "DirectX Shader Compiler release (DXIL output)")
set(PERLIN_DXC_STAMP "2025_02_20" CACHE STRING "Date stamp in the DXC release file names")
set(PERLIN_SLANGC "" CACHE FILEPATH "slangc to compile shaders with; fetched for the build machine when empty")
set(PERLIN_DXC_LIBS "" CACHE PATH "directory with dxcompiler and dxil for DXIL output; fetched when empty")

# a pack compiled on another machine for the same backends (FreeBSD: slang ships no FreeBSD compiler, so CI builds
# the pack on Linux, whose backends are the same, and hands it over)
set(PERLIN_SHADER_PACK_PREBUILT "" CACHE FILEPATH "perlin_shaders.pack built elsewhere for the same graphics backends")
if(PERLIN_SHADER_PACK_PREBUILT)
    set(PERLIN_SHADER_PACK "${CMAKE_CURRENT_BINARY_DIR}/shaders/perlin_shaders.pack")
    add_custom_command(OUTPUT "${PERLIN_SHADER_PACK}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PERLIN_SHADER_PACK_PREBUILT}" "${PERLIN_SHADER_PACK}"
        DEPENDS "${PERLIN_SHADER_PACK_PREBUILT}" COMMENT "Using the prebuilt shader pack" VERBATIM)
    add_custom_target(perlin_shader_pack DEPENDS "${PERLIN_SHADER_PACK}")
    return()
endif()

if(NOT PERLIN_SLANGC)
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
    CPMAddPackage(NAME slang
        URL "https://github.com/shader-slang/slang/releases/download/v${PERLIN_SLANG_VERSION}/slang-${PERLIN_SLANG_VERSION}-${_slang_os}-${_slang_arch}.${_slang_ext}"
        DOWNLOAD_ONLY YES)
    if(CMAKE_HOST_WIN32)
        set(PERLIN_SLANGC "${slang_SOURCE_DIR}/bin/slangc.exe" CACHE FILEPATH "" FORCE)
    else()
        set(PERLIN_SLANGC "${slang_SOURCE_DIR}/bin/slangc" CACHE FILEPATH "" FORCE)
    endif()
endif()

# only what the enabled backends read: spirv (vulkan), dxil (d3d12), hlsl (d3d11), msl (metal), wgsl (webgpu)
set(PERLIN_SHADER_TARGETS)
if(PERLIN_RHI_VULKAN)
    list(APPEND PERLIN_SHADER_TARGETS spirv)
endif()
if(PERLIN_RHI_D3D11)
    list(APPEND PERLIN_SHADER_TARGETS hlsl)
endif()
if(PERLIN_RHI_METAL)
    list(APPEND PERLIN_SHADER_TARGETS msl)
endif()
if(PERLIN_RHI_WEBGPU)
    list(APPEND PERLIN_SHADER_TARGETS wgsl)
endif()
if(PERLIN_RHI_D3D12 AND NOT PERLIN_DXC_LIBS)
    if(CMAKE_HOST_WIN32)
        CPMAddPackage(NAME dxc
            URL "https://github.com/microsoft/DirectXShaderCompiler/releases/download/${PERLIN_DXC_RELEASE}/dxc_${PERLIN_DXC_STAMP}.zip"
            DOWNLOAD_ONLY YES)
        set(PERLIN_DXC_LIBS "${dxc_SOURCE_DIR}/bin/x64" CACHE PATH "" FORCE)
    elseif(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$" AND NOT CMAKE_HOST_APPLE)
        CPMAddPackage(NAME dxc
            URL "https://github.com/microsoft/DirectXShaderCompiler/releases/download/${PERLIN_DXC_RELEASE}/linux_dxc_${PERLIN_DXC_STAMP}.x86_64.tar.gz"
            DOWNLOAD_ONLY YES)
        set(PERLIN_DXC_LIBS "${dxc_SOURCE_DIR}/lib" CACHE PATH "" FORCE)
    endif()
endif()
set(_dxc_args)
if(PERLIN_RHI_D3D12)
    if(PERLIN_DXC_LIBS)
        list(APPEND PERLIN_SHADER_TARGETS dxil)
        set(_dxc_args --library-path "${PERLIN_DXC_LIBS}")
    else()
        message(STATUS "Perlin: no DXC for this machine, the shader pack will not carry DXIL (Direct3D 12 needs it)")
    endif()
endif()
if(NOT PERLIN_SHADER_TARGETS)
    message(FATAL_ERROR "Perlin: no graphics backend enabled (PERLIN_RHI_VULKAN / D3D12 / D3D11 / METAL / WEBGPU)")
endif()
list(JOIN PERLIN_SHADER_TARGETS "," _targets)

set(PERLIN_SLANG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/slang")
file(GLOB PERLIN_SLANG_SOURCES CONFIGURE_DEPENDS "${PERLIN_SLANG_ROOT}/*.slang" "${PERLIN_SLANG_ROOT}/programs/*.slang"
     "${PERLIN_RD_SHADER_DIR}/*.slang")
set(PERLIN_SHADER_PACK "${CMAKE_CURRENT_BINARY_DIR}/shaders/perlin_shaders.pack")
add_custom_command(
    OUTPUT "${PERLIN_SHADER_PACK}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/pack_shaders.py"
            --slangc "${PERLIN_SLANGC}" -I "${PERLIN_SLANG_ROOT}" -I "${PERLIN_RD_SHADER_DIR}" ${_dxc_args}
            --slots "${PERLIN_SLANG_ROOT}/texture_slots.json" --targets "${_targets}"
            --cache "${CMAKE_CURRENT_BINARY_DIR}/shaders/cache"
            -o "${PERLIN_SHADER_PACK}" "${PERLIN_SLANG_ROOT}/programs" "${PERLIN_RD_SHADER_DIR}"
    DEPENDS ${PERLIN_SLANG_SOURCES} "${PERLIN_SLANG_ROOT}/texture_slots.json" "${CMAKE_CURRENT_SOURCE_DIR}/tools/pack_shaders.py"
    COMMENT "Compiling slang shaders (${_targets})"
    VERBATIM)
add_custom_target(perlin_shader_pack DEPENDS "${PERLIN_SHADER_PACK}")
