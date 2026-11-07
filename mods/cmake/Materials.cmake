# Keep upstream shaders in the dependency cache, not in Perlin's resources/.
set(PERLIN_MCBE_SHADER_REVISION bf4c3467b384f74a13e045f759f0f21e837c5536)
set(PERLIN_MCBE_SHADER_SOURCE_DIR "" CACHE PATH "Existing mcbe-shader-codebase release/obfuscated checkout")
if(NOT PERLIN_MCBE_SHADER_SOURCE_DIR)
    CPMAddPackage(
        NAME mcbe_shaders
        URL "https://codeload.github.com/veka0/mcbe-shader-codebase/tar.gz/${PERLIN_MCBE_SHADER_REVISION}"
        URL_HASH SHA256=e3ba054120b0984ae88941d2be21bd94dbcfdb6f53c0ea44d1d6a444452957a0
        DOWNLOAD_ONLY YES)
    set(PERLIN_MCBE_SHADER_SOURCE_DIR "${mcbe_shaders_SOURCE_DIR}")
endif()
set(PERLIN_MATERIAL_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/materials")
file(GLOB_RECURSE perlin_material_sources CONFIGURE_DEPENDS
    "${PERLIN_MCBE_SHADER_SOURCE_DIR}/materials/*.glsl")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${perlin_material_sources} "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_material_catalog.py")
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_material_catalog.py"
            "${PERLIN_MCBE_SHADER_SOURCE_DIR}" "${PERLIN_MATERIAL_INCLUDE_DIR}/MaterialCatalog.hpp"
    COMMAND_ERROR_IS_FATAL ANY)
set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/client/renderer/MaterialDb.cpp"
    APPEND PROPERTY INCLUDE_DIRECTORIES "${PERLIN_MATERIAL_INCLUDE_DIR}")

# Adapt the upstream pixel programs to Perlin's mesh
# and sampler interfaces. The generated programs replace the old game shaders.
set(PERLIN_RD_SHADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/renderdragon")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_renderdragon_shaders.py")
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_renderdragon_shaders.py"
            "${PERLIN_MCBE_SHADER_SOURCE_DIR}" "${PERLIN_RD_SHADER_DIR}"
    COMMAND_ERROR_IS_FATAL ANY)
