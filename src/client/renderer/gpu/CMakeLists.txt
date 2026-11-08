cmake_minimum_required(VERSION 3.28)
if(NOT TARGET perlin_gpu)
    if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
        project(perlin_gpu LANGUAGES C CXX)
        set(CMAKE_CXX_STANDARD 23)
        set(CMAKE_CXX_STANDARD_REQUIRED ON)
        set(CMAKE_CXX_EXTENSIONS OFF)
        set(PERLIN_GPU_STANDALONE ON)
        add_compile_definitions(GLM_FORCE_DEPTH_ZERO_TO_ONE)
        set(PERLIN_GLM_DIR "" CACHE PATH "glm checkout for standalone builds")
        set(PERLIN_GLSLANG_DIR "" CACHE PATH "glslang checkout (java shader packs)")
        set(PERLIN_SPIRV_CROSS_DIR "" CACHE PATH "SPIRV-Cross checkout (java shader packs)")
        set(PERLIN_MINIZ_DIR "" CACHE PATH "miniz checkout (zipped java shader packs)")
        set(PERLIN_STB_DIR "" CACHE PATH "stb checkout (pack textures)")
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../../rhi" rhi)
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../../render" render)
        enable_testing()
        set(glslang_SOURCE_DIR "${PERLIN_GLSLANG_DIR}")
        set(SPIRV-Cross_SOURCE_DIR "${PERLIN_SPIRV_CROSS_DIR}")
        set(stb_SOURCE_DIR "${PERLIN_STB_DIR}")
        include("${CMAKE_CURRENT_SOURCE_DIR}/../../../../cmake/PerlinJavaShaders.cmake")

        file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/miniz_gen/miniz_export.h" "#pragma once\n#define MINIZ_EXPORT\n")
        add_library(miniz STATIC "${PERLIN_MINIZ_DIR}/miniz.c" "${PERLIN_MINIZ_DIR}/miniz_tdef.c" "${PERLIN_MINIZ_DIR}/miniz_tinfl.c"
                    "${PERLIN_MINIZ_DIR}/miniz_zip.c")
        target_include_directories(miniz PUBLIC "${PERLIN_MINIZ_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/miniz_gen")
    endif()

    file(GLOB PERLIN_JSHADER_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../../javashaders/*.cpp")
    add_library(perlin_gpu STATIC
        ${PERLIN_JSHADER_SOURCES}
        JavaPack.cpp
        GpuCore.cpp
        ModGpuHub.cpp
        ModGpu.cpp
        ModGpuRun.cpp
        ChunkMeshStore.cpp
        GpuChunkCull.cpp
        RenderContext_rhi.cpp
        RenderContext_rhi_java.cpp
        RenderContext_rhi_pack.cpp
        RenderContext_rhi_post.cpp
        RenderContext_rhi_shadow.cpp
        RenderContext_rhi_ui.cpp
        RenderContext_rhi_world.cpp
        ../../../util/GraphicsDebugger.cpp)
    target_include_directories(perlin_gpu PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../../..")
    if(PERLIN_API_INCLUDE_DIRS)
        target_include_directories(perlin_gpu PUBLIC ${PERLIN_API_INCLUDE_DIRS})
    elseif(PERLIN_API_DIR)
        target_include_directories(perlin_gpu PUBLIC "${PERLIN_API_DIR}/sdk")
    endif()

    find_package(Threads REQUIRED)
    target_link_libraries(perlin_gpu PUBLIC conduit::conduit perlin_glslang perlin_spirv_cross miniz Threads::Threads ${CMAKE_DL_LIBS})
    if(CMAKE_SYSTEM_NAME MATCHES "BSD")

        target_compile_options(perlin_gpu PUBLIC -pthread)
        target_link_options(perlin_gpu INTERFACE -pthread)
    endif()
    if(stb_SOURCE_DIR)
        target_include_directories(perlin_gpu PRIVATE "${stb_SOURCE_DIR}")
    endif()
    if(PERLIN_GLM_DIR)
        target_include_directories(perlin_gpu PUBLIC "${PERLIN_GLM_DIR}")
    endif()
endif()

if(PERLIN_GPU_STANDALONE)
    set(PERLIN_MCBE_SHADER_SOURCE_DIR "" CACHE PATH "mcbe-shader-codebase checkout for the material catalog")
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(PERLIN_MATERIAL_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/materials")
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../../../../tools/gen_material_catalog.py"
                "${PERLIN_MCBE_SHADER_SOURCE_DIR}" "${PERLIN_MATERIAL_INCLUDE_DIR}/MaterialCatalog.hpp"
        COMMAND_ERROR_IS_FATAL ANY)
    set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../MaterialDb.cpp" APPEND PROPERTY INCLUDE_DIRECTORIES "${PERLIN_MATERIAL_INCLUDE_DIR}")
    add_executable(gpu_context_test
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../../tests/gpu_context_test.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../../tests/gpu_context_stubs.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../Tessellator.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../SkyGeometry.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../MaterialDb.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../ui/Json.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../util/Paths.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../util/Resources.cpp")
    target_include_directories(gpu_context_test BEFORE PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../../../tests/stubs")
    target_sources(gpu_context_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../javashaders/PackSource.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../javashaders/PackSourceZip.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../javashaders/Properties.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../javashaders/Cond.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../resources/TextureLoader.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../world/level/block/BlockPalette.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../util/Json.cpp")
    target_include_directories(gpu_context_test PRIVATE "${stb_SOURCE_DIR}")
    target_link_libraries(gpu_context_test PRIVATE glaze::glaze miniz)
    target_link_libraries(gpu_context_test PRIVATE perlin_gpu)

    set(PERLIN_TEST_PACK "" CACHE FILEPATH "shader pack for gpu_context_test")
    if(PERLIN_TEST_PACK)
        foreach(api VULKAN WEBGPU D3D12 D3D11 METAL)
            if(CONDUIT_${api})
                string(TOLOWER ${api} id)
                add_test(NAME gpu_context_${id} COMMAND gpu_context_test "${PERLIN_TEST_PACK}" ${id})
                set_tests_properties(gpu_context_${id} PROPERTIES SKIP_RETURN_CODE 77)
            endif()
        endforeach()
    endif()
endif()
