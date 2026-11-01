# backend-neutral rhi; usable on its own (cmake -S src/rhi) or from the root build
cmake_minimum_required(VERSION 3.28)
if(NOT TARGET perlin_rhi)
    if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
        project(perlin_rhi LANGUAGES C CXX)
        set(CMAKE_CXX_STANDARD 23)
        set(CMAKE_CXX_STANDARD_REQUIRED ON)
        set(CMAKE_CXX_EXTENSIONS OFF)
        set(PERLIN_RHI_STANDALONE ON)
    endif()

    option(PERLIN_RHI_VULKAN "build the vulkan backend" ON)
    option(PERLIN_RHI_D3D12 "build the direct3d 12 backend" ${WIN32})
    option(PERLIN_RHI_D3D11 "build the direct3d 11 backend" OFF)
    option(PERLIN_RHI_METAL "build the metal backend" OFF)
    option(PERLIN_RHI_WEBGPU "build the webgpu backend" OFF)
    # headers only: the streamline dlls are loaded at run time and shipped beside the executable
    # nvidia ships the streamline dlls for x64 only
    set(_perlin_rhi_streamline_default OFF)
    if(WIN32 AND NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|arm64|aarch64)$" AND NOT CMAKE_CXX_COMPILER_TARGET MATCHES "^(aarch64|arm64)")
        set(_perlin_rhi_streamline_default ON)
    endif()
    option(PERLIN_RHI_STREAMLINE "build the dlss / reflex / frame generation layer" ${_perlin_rhi_streamline_default})
    # offline builds point these at checkouts; otherwise the pinned revisions are fetched
    set(PERLIN_RHI_DEPS_DIR "" CACHE PATH "directory holding Vulkan-Headers, volk, vma, d3d12ma, DirectX-Headers checkouts")

    set(PERLIN_RHI_SOURCES
        Common.cpp
        ShaderLibrary.cpp
        DeviceProfile.cpp
        vendor/Vendor.cpp
        common/StateValidator.cpp
        common/ShaderReflect.cpp
        null/NullDevice.cpp)

    include(FetchContent)
    function(perlin_rhi_dep name dir repo tag)
        if(PERLIN_RHI_DEPS_DIR AND EXISTS "${PERLIN_RHI_DEPS_DIR}/${dir}")
            set(FETCHCONTENT_SOURCE_DIR_${name} "${PERLIN_RHI_DEPS_DIR}/${dir}" CACHE PATH "" FORCE)
        endif()
        # a shallow clone only has the branch tip: a pinned commit that is no longer the tip cannot be checked out
        # ("fatal: unable to read tree"), so commit pins take the whole history and tags stay shallow
        if("${tag}" MATCHES "^[0-9a-f]+$" AND NOT "${tag}" MATCHES "^[0-9]+$")
            set(shallow FALSE)
        else()
            set(shallow TRUE)
        endif()
        FetchContent_Declare(${name} GIT_REPOSITORY ${repo} GIT_TAG ${tag} GIT_SHALLOW ${shallow})
        FetchContent_GetProperties(${name})
        if(NOT ${name}_POPULATED)
            FetchContent_Populate(${name})
        endif()
        set(${name}_SOURCE_DIR "${${name}_SOURCE_DIR}" PARENT_SCOPE)
    endfunction()

    if(PERLIN_RHI_VULKAN)
        perlin_rhi_dep(vulkan_headers Vulkan-Headers https://github.com/KhronosGroup/Vulkan-Headers.git v1.4.366)
        perlin_rhi_dep(volk volk https://github.com/zeux/volk.git b7764bc7f5880139859a15d1609d2ee0c6251608)
        perlin_rhi_dep(vma vma https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git v3.5.0)
        list(APPEND PERLIN_RHI_SOURCES
            vulkan/VulkanInstance.cpp
            vulkan/VulkanDevice.cpp
            vulkan/VulkanCommands.cpp
            vulkan/VulkanSwapchain.cpp
            vulkan/VmaImpl.cpp
            "${volk_SOURCE_DIR}/volk.c")
    endif()

    if(PERLIN_RHI_D3D12)
        perlin_rhi_dep(directx_headers DirectX-Headers https://github.com/microsoft/DirectX-Headers.git faf928dc6feb8b86e9e3d6a39fc5171de7d8ac36)
        perlin_rhi_dep(d3d12ma d3d12ma https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator.git v3.2.1)
        list(APPEND PERLIN_RHI_SOURCES
            d3d12/D3D12Instance.cpp
            d3d12/D3D12Device.cpp
            d3d12/D3D12Commands.cpp
            d3d12/D3D12Swapchain.cpp
            d3d12/D3D12MaImpl.cpp)
        # toolchains without declspec(uuid) (mingw, clang) take interface ids from this
        if(NOT MSVC)
            list(APPEND PERLIN_RHI_SOURCES "${directx_headers_SOURCE_DIR}/src/dxguids.cpp")
        endif()
    endif()

    if(PERLIN_RHI_D3D11)
        list(APPEND PERLIN_RHI_SOURCES
            d3d11/D3D11Instance.cpp
            d3d11/D3D11Device.cpp
            d3d11/D3D11Commands.cpp)
    endif()

    if(PERLIN_RHI_METAL)
        list(APPEND PERLIN_RHI_SOURCES
            metal/MetalConvert.mm
            metal/MetalDevice.mm
            metal/MetalCommands.mm
            metal/MetalInstance.mm)
        # compiled as c++ with the objective-c++ dialect forced, so no separate OBJCXX language is needed: enabling that one
        # leaves cmake without its compile and archive rules on the macos runners
        set_source_files_properties(metal/MetalConvert.mm metal/MetalDevice.mm metal/MetalCommands.mm metal/MetalInstance.mm
            PROPERTIES LANGUAGE CXX COMPILE_OPTIONS "-x;objective-c++;-fobjc-arc")
    endif()

    if(PERLIN_RHI_WEBGPU)
        list(APPEND PERLIN_RHI_SOURCES
            webgpu/WebGpuConvert.cpp
            webgpu/WebGpuDevice.cpp
            webgpu/WebGpuPipeline.cpp
            webgpu/WebGpuCommands.cpp
            webgpu/WebGpuInstance.cpp)
        if(NOT EMSCRIPTEN)
            # prebuilt wgpu-native: a directory holding include/webgpu and lib/, or the release archive for this platform
            set(PERLIN_RHI_WGPU_DIR "" CACHE PATH "wgpu-native install (include/webgpu/webgpu.h, lib/)")
            if(NOT PERLIN_RHI_WGPU_DIR AND PERLIN_RHI_DEPS_DIR AND EXISTS "${PERLIN_RHI_DEPS_DIR}/wgpu")
                set(PERLIN_RHI_WGPU_DIR "${PERLIN_RHI_DEPS_DIR}/wgpu")
            endif()
            if(NOT PERLIN_RHI_WGPU_DIR)
                set(wgpu_tag v25.0.2.2)
                string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" wgpu_cpu)
                if(APPLE AND CMAKE_OSX_ARCHITECTURES)
                    string(TOLOWER "${CMAKE_OSX_ARCHITECTURES}" wgpu_cpu)
                endif()
                if(ANDROID)
                    set(wgpu_cpu "${ANDROID_ABI}")
                endif()
                if(wgpu_cpu MATCHES "aarch64|arm64")
                    set(wgpu_arch aarch64)
                elseif(wgpu_cpu MATCHES "armeabi|armv7")
                    set(wgpu_arch armv7)
                else()
                    set(wgpu_arch x86_64)
                endif()
                if(WIN32)
                    # the msvc archive carries the import library; the gnu one a static library
                    if(MSVC OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
                        set(wgpu_name wgpu-windows-${wgpu_arch}-msvc-release)
                    else()
                        set(wgpu_name wgpu-windows-${wgpu_arch}-gnu-release)
                    endif()
                elseif(APPLE)
                    set(wgpu_name wgpu-macos-${wgpu_arch}-release)
                elseif(ANDROID)
                    set(wgpu_name wgpu-android-${wgpu_arch}-release)
                else()
                    set(wgpu_name wgpu-linux-${wgpu_arch}-release)
                endif()
                FetchContent_Declare(wgpu_native URL https://github.com/gfx-rs/wgpu-native/releases/download/${wgpu_tag}/${wgpu_name}.zip)
                FetchContent_MakeAvailable(wgpu_native)
                set(PERLIN_RHI_WGPU_DIR "${wgpu_native_SOURCE_DIR}")
            endif()
        endif()
    endif()

    if(PERLIN_RHI_STREAMLINE AND WIN32)
        perlin_rhi_dep(streamline Streamline https://github.com/NVIDIAGameWorks/Streamline.git 625db07d4bca5abc67d781c42af7e7fb0761f1b6)
        list(APPEND PERLIN_RHI_SOURCES vendor/Streamline.cpp)
    endif()

    add_library(perlin_rhi STATIC ${PERLIN_RHI_SOURCES})
    target_include_directories(perlin_rhi PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
    target_compile_features(perlin_rhi PUBLIC cxx_std_23)
    if(WIN32)
        target_compile_definitions(perlin_rhi PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
    endif()

    if(PERLIN_RHI_STREAMLINE AND WIN32)
        target_compile_definitions(perlin_rhi PUBLIC RHI_HAS_STREAMLINE=1)
        target_include_directories(perlin_rhi PRIVATE "${streamline_SOURCE_DIR}/include")
        if(MINGW)
            # mingw headers spell softpub.h in lower case on a case sensitive file system
            target_include_directories(perlin_rhi PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/vendor/compat")
        endif()
        # the sdk headers use std::to_underlying only when __cplusplus is exactly 202302L, and the msvc stl hides it
        # unless /std:c++latest is given; building this one file as c++20 makes them use their own helper
        if(MSVC OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
            set_source_files_properties(vendor/Streamline.cpp PROPERTIES COMPILE_OPTIONS "-clang:-std=c++20")
        else()
            set_source_files_properties(vendor/Streamline.cpp PROPERTIES COMPILE_OPTIONS "-std=c++20")
        endif()
        if(PERLIN_RHI_VULKAN)
            target_include_directories(perlin_rhi PRIVATE "${vulkan_headers_SOURCE_DIR}/include")
        endif()
    endif()

    foreach(api VULKAN D3D12 D3D11 METAL WEBGPU)
        if(PERLIN_RHI_${api})
            target_compile_definitions(perlin_rhi PUBLIC RHI_HAS_${api}=1)
        endif()
    endforeach()

    if(PERLIN_RHI_VULKAN)
        target_include_directories(perlin_rhi PRIVATE
            "${vulkan_headers_SOURCE_DIR}/include" "${volk_SOURCE_DIR}" "${vma_SOURCE_DIR}/include")
        # nothing links the loader: volk finds it at run time and the rhi reports "no vulkan" when it is missing
        target_compile_definitions(perlin_rhi PRIVATE VK_NO_PROTOTYPES)
        if(UNIX AND NOT APPLE)
            target_link_libraries(perlin_rhi PUBLIC ${CMAKE_DL_LIBS})
        endif()
    endif()

    if(PERLIN_RHI_WEBGPU)
        if(EMSCRIPTEN)
            # emdawnwebgpu supplies webgpu.h and the js glue
            target_compile_options(perlin_rhi PUBLIC --use-port=emdawnwebgpu)
            target_link_options(perlin_rhi PUBLIC --use-port=emdawnwebgpu)
        else()
            target_include_directories(perlin_rhi PUBLIC "${PERLIN_RHI_WGPU_DIR}/include" "${PERLIN_RHI_WGPU_DIR}/include/webgpu")
            if(WIN32 AND (MSVC OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"))
                # a static rust library would pull its own c runtime in next to ours: use the dll and its import library
                find_library(PERLIN_WGPU_LIB NAMES wgpu_native.dll.lib HINTS "${PERLIN_RHI_WGPU_DIR}/lib" NO_DEFAULT_PATH)
                set(PERLIN_RHI_RUNTIME_DLL "${PERLIN_RHI_WGPU_DIR}/lib/wgpu_native.dll" CACHE FILEPATH "dll to ship beside the executable" FORCE)
            else()
                # static on purpose: nothing but system libraries may be needed at run time
                find_library(PERLIN_WGPU_LIB NAMES libwgpu_native.a HINTS "${PERLIN_RHI_WGPU_DIR}/lib" NO_DEFAULT_PATH)
            endif()
            if(NOT PERLIN_WGPU_LIB)
                message(FATAL_ERROR "wgpu-native library not found under ${PERLIN_RHI_WGPU_DIR}/lib")
            endif()
            target_link_libraries(perlin_rhi PUBLIC "${PERLIN_WGPU_LIB}")
            if(WIN32)
                target_link_libraries(perlin_rhi PUBLIC ws2_32 userenv ntdll bcrypt d3dcompiler opengl32 gdi32 ole32 oleaut32 propsys runtimeobject)
            elseif(APPLE)
                target_link_libraries(perlin_rhi PUBLIC "-framework Metal" "-framework QuartzCore" "-framework Foundation"
                    "-framework CoreFoundation" "-framework CoreGraphics" "-framework AppKit" "-framework IOSurface" "-framework Security" objc iconv)
            elseif(ANDROID)
                target_link_libraries(perlin_rhi PUBLIC android log m ${CMAKE_DL_LIBS})
            elseif(UNIX)
                target_link_libraries(perlin_rhi PUBLIC ${CMAKE_DL_LIBS} m pthread)
            endif()
        endif()
    endif()

    if(PERLIN_RHI_METAL)
        target_link_libraries(perlin_rhi PUBLIC "-framework Metal" "-framework QuartzCore" "-framework Foundation" "-framework CoreGraphics")
    endif()

    if(PERLIN_RHI_D3D12)
        # directx/ must come first: it supplies d3d12.h for toolchains whose own headers are older or missing
        target_include_directories(perlin_rhi PRIVATE
            "${directx_headers_SOURCE_DIR}/include" "${directx_headers_SOURCE_DIR}/include/directx"
            "${d3d12ma_SOURCE_DIR}/include" "${d3d12ma_SOURCE_DIR}/src")
    endif()

    if(PERLIN_RHI_STANDALONE)
        enable_testing()
        add_executable(rhi_null_selftest "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/rhi_null_selftest.cpp")
        target_link_libraries(rhi_null_selftest PRIVATE perlin_rhi)
        add_test(NAME rhi_null_selftest COMMAND rhi_null_selftest)
        add_executable(rhi_profile_test "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/rhi_profile_test.cpp")
        target_link_libraries(rhi_profile_test PRIVATE perlin_rhi)
        add_test(NAME rhi_profile_test COMMAND rhi_profile_test)
        add_executable(rhi_vendor_test "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/rhi_vendor_test.cpp")
        target_link_libraries(rhi_vendor_test PRIVATE perlin_rhi)
        add_test(NAME rhi_vendor_test COMMAND rhi_vendor_test)
        find_program(PERLIN_SLANGC slangc HINTS "${PERLIN_RHI_DEPS_DIR}/slang/bin")
        find_package(Python3 COMPONENTS Interpreter)
        if(PERLIN_SLANGC AND Python3_FOUND)
            set(shader_root "${CMAKE_CURRENT_SOURCE_DIR}/../shaders/slang")
            file(GLOB shader_inputs CONFIGURE_DEPENDS "${shader_root}/*.slang" "${shader_root}/programs/*.slang")
            set(dxc_libs)
            if(PERLIN_RHI_DEPS_DIR AND EXISTS "${PERLIN_RHI_DEPS_DIR}/dxc/lib")
                set(dxc_libs --library-path "${PERLIN_RHI_DEPS_DIR}/dxc/lib")
            endif()
            add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/test_shaders.pack"
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/pack_shaders.py"
                        --slangc "${PERLIN_SLANGC}" -I "${shader_root}" ${dxc_libs} --slots "${shader_root}/texture_slots.json"
                        -o "${CMAKE_CURRENT_BINARY_DIR}/test_shaders.pack" "${shader_root}/programs"
                DEPENDS ${shader_inputs} "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/pack_shaders.py" VERBATIM)
            add_custom_target(rhi_shader_pack DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/test_shaders.pack")
            add_executable(rhi_shaderlib_test "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/rhi_shaderlib_test.cpp")
            target_link_libraries(rhi_shaderlib_test PRIVATE perlin_rhi)
            add_dependencies(rhi_shaderlib_test rhi_shader_pack)
            add_test(NAME rhi_shaderlib_test COMMAND rhi_shaderlib_test "${CMAKE_CURRENT_BINARY_DIR}/test_shaders.pack")
            add_executable(rhi_pipeline_smoke "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/rhi_pipeline_smoke.cpp")
            target_link_libraries(rhi_pipeline_smoke PRIVATE perlin_rhi)
            add_dependencies(rhi_pipeline_smoke rhi_shader_pack)
            if(PERLIN_RHI_VULKAN)
                add_test(NAME rhi_pipeline_smoke_vulkan COMMAND rhi_pipeline_smoke "${CMAKE_CURRENT_BINARY_DIR}/test_shaders.pack" vulkan)
                set_tests_properties(rhi_pipeline_smoke_vulkan PROPERTIES SKIP_RETURN_CODE 77)
            endif()
            if(PERLIN_RHI_D3D12)
                add_test(NAME rhi_pipeline_smoke_d3d12 COMMAND rhi_pipeline_smoke "${CMAKE_CURRENT_BINARY_DIR}/test_shaders.pack" d3d12)
                set_tests_properties(rhi_pipeline_smoke_d3d12 PROPERTIES SKIP_RETURN_CODE 77)
            endif()
        endif()
        if(PERLIN_RHI_VULKAN OR PERLIN_RHI_D3D12)
            add_executable(rhi_gpu_selftest "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/rhi_gpu_selftest.cpp")
            target_link_libraries(rhi_gpu_selftest PRIVATE perlin_rhi)
            # test shaders are slang like the game's; without slangc the gpu tests are not registered
            find_program(PERLIN_SLANGC slangc HINTS "${PERLIN_RHI_DEPS_DIR}/slang/bin")
            if(PERLIN_SLANGC)
                set(shader_src "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/rhi/shaders")
                set(shader_out "${CMAKE_CURRENT_BINARY_DIR}/test_shaders")
                set(shader_files)
                foreach(entry "raster.slang;vsMain;vertex;raster.vs" "raster.slang;fsMain;fragment;raster.fs" "cull.slang;csMain;compute;cull.cs")
                    list(GET entry 0 file)
                    list(GET entry 1 name)
                    list(GET entry 2 stage)
                    list(GET entry 3 out)
                    foreach(target spirv dxil)
                        set(ext spv)
                        set(extra)
                        if(target STREQUAL dxil)
                            set(ext dxil)
                            set(extra -profile sm_6_2)
                        endif()
                        add_custom_command(OUTPUT "${shader_out}/${out}.${ext}"
                            COMMAND ${CMAKE_COMMAND} -E make_directory "${shader_out}"
                            COMMAND "${PERLIN_SLANGC}" "${shader_src}/${file}" -target ${target} ${extra} -entry ${name} -stage ${stage}
                                    -o "${shader_out}/${out}.${ext}"
                            DEPENDS "${shader_src}/${file}" VERBATIM)
                        list(APPEND shader_files "${shader_out}/${out}.${ext}")
                    endforeach()
                endforeach()
                add_custom_target(rhi_test_shaders DEPENDS ${shader_files})
                add_dependencies(rhi_gpu_selftest rhi_test_shaders)
                if(PERLIN_RHI_VULKAN)
                    add_test(NAME rhi_gpu_selftest_vulkan COMMAND rhi_gpu_selftest "${shader_out}" vulkan)
                    set_tests_properties(rhi_gpu_selftest_vulkan PROPERTIES SKIP_RETURN_CODE 77)
                endif()
                if(PERLIN_RHI_D3D12)
                    add_test(NAME rhi_gpu_selftest_d3d12 COMMAND rhi_gpu_selftest "${shader_out}" d3d12)
                    set_tests_properties(rhi_gpu_selftest_d3d12 PROPERTIES SKIP_RETURN_CODE 77)
                endif()
            endif()
        endif()
    endif()
endif()
