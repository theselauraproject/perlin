# glslang and SPIRV-Cross as static libraries, for the java shader pack frontend (GLSL -> SPIR-V -> HLSL).
# needs glslang_SOURCE_DIR and SPIRV-Cross_SOURCE_DIR; shared by the app build and the standalone gpu build
if(NOT TARGET perlin_glslang)
    set(PERLIN_GLSLANG_GEN "${CMAKE_BINARY_DIR}/perlin_glslang_gen")
    file(WRITE "${PERLIN_GLSLANG_GEN}/glslang/build_info.h" "#pragma once\n#define GLSLANG_VERSION_MAJOR 16\n#define GLSLANG_VERSION_MINOR 6\n#define GLSLANG_VERSION_PATCH 0\n#define GLSLANG_VERSION_FLAVOR \"\"\n#define GLSLANG_VERSION_GREATER_THAN(major, minor, patch) ((GLSLANG_VERSION_MAJOR) > (major) || ((major) == GLSLANG_VERSION_MAJOR && ((GLSLANG_VERSION_MINOR) > (minor) || ((minor) == GLSLANG_VERSION_MINOR && (GLSLANG_VERSION_PATCH) > (patch)))))\n#define GLSLANG_VERSION_GREATER_OR_EQUAL_TO(major, minor, patch) (GLSLANG_VERSION_GREATER_THAN(major, minor, patch) || ((major) == GLSLANG_VERSION_MAJOR && (minor) == GLSLANG_VERSION_MINOR && (patch) == GLSLANG_VERSION_PATCH))\n#define GLSLANG_VERSION_LESS_THAN(major, minor, patch) (!GLSLANG_VERSION_GREATER_OR_EQUAL_TO(major, minor, patch))\n#define GLSLANG_VERSION_LESS_OR_EQUAL_TO(major, minor, patch) (!GLSLANG_VERSION_GREATER_THAN(major, minor, patch))\n")
    file(GLOB PERLIN_GLSLANG_SOURCES
        "${glslang_SOURCE_DIR}/glslang/GenericCodeGen/*.cpp"
        "${glslang_SOURCE_DIR}/glslang/MachineIndependent/*.cpp"
        "${glslang_SOURCE_DIR}/glslang/MachineIndependent/preprocessor/*.cpp"
        "${glslang_SOURCE_DIR}/glslang/ResourceLimits/*.cpp"
        "${glslang_SOURCE_DIR}/glslang/CInterface/*.cpp"
        "${glslang_SOURCE_DIR}/SPIRV/*.cpp")
    # the hlsl front end is only for tools/jshader_check, which validates the generated hlsl without fxc / dxc
    if(PERLIN_BUILD_TESTS)
        file(GLOB PERLIN_GLSLANG_HLSL_SOURCES "${glslang_SOURCE_DIR}/glslang/HLSL/*.cpp")
        list(APPEND PERLIN_GLSLANG_SOURCES ${PERLIN_GLSLANG_HLSL_SOURCES})
    endif()
    if(WIN32)
        list(APPEND PERLIN_GLSLANG_SOURCES "${glslang_SOURCE_DIR}/glslang/OSDependent/Windows/ossource.cpp")
    else()
        list(APPEND PERLIN_GLSLANG_SOURCES "${glslang_SOURCE_DIR}/glslang/OSDependent/Unix/ossource.cpp")
    endif()
    add_library(perlin_glslang STATIC ${PERLIN_GLSLANG_SOURCES})
    target_include_directories(perlin_glslang PUBLIC "${glslang_SOURCE_DIR}" "${PERLIN_GLSLANG_GEN}")
    target_compile_definitions(perlin_glslang PRIVATE ENABLE_OPT=0)
    if(PERLIN_BUILD_TESTS)
        target_compile_definitions(perlin_glslang PRIVATE ENABLE_HLSL=1)
    endif()
    set_target_properties(perlin_glslang PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_STANDARD 17)
    if(MSVC)
        target_compile_options(perlin_glslang PRIVATE /w /EHsc)
    else()
        target_compile_options(perlin_glslang PRIVATE -w)
    endif()
    add_library(perlin_spirv_cross STATIC
        "${SPIRV-Cross_SOURCE_DIR}/spirv_cross.cpp" "${SPIRV-Cross_SOURCE_DIR}/spirv_cfg.cpp"
        "${SPIRV-Cross_SOURCE_DIR}/spirv_cross_parsed_ir.cpp" "${SPIRV-Cross_SOURCE_DIR}/spirv_parser.cpp"
        "${SPIRV-Cross_SOURCE_DIR}/spirv_glsl.cpp" "${SPIRV-Cross_SOURCE_DIR}/spirv_hlsl.cpp"
        "${SPIRV-Cross_SOURCE_DIR}/spirv_msl.cpp")
    target_include_directories(perlin_spirv_cross PUBLIC "${SPIRV-Cross_SOURCE_DIR}")
    set_target_properties(perlin_spirv_cross PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_STANDARD 17)
    if(MSVC)
        target_compile_options(perlin_spirv_cross PRIVATE /w /EHsc)
    else()
        target_compile_options(perlin_spirv_cross PRIVATE -w)
    endif()
endif()
