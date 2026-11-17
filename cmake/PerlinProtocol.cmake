set(PERLIN_PROTOCOL_OUT "${CMAKE_CURRENT_BINARY_DIR}/generated/protocol")
set(PERLIN_PROTOCOL_VERSION 2225)
set(PERLIN_PROTOCOL_CODEC_COUNT 6)
set(PERLIN_PROTOCOL_LEGACY_PARTS 2)
set(PERLIN_PROTOCOL_LEGACY 944=1.26.10 975=1.26.20 1001=1.26.30 2168=1.26.40 2193=1.26.51)
set(PERLIN_PROTOCOL_SOURCES)
foreach(i RANGE 0 5)
    list(APPEND PERLIN_PROTOCOL_SOURCES "${PERLIN_PROTOCOL_OUT}/Codec${i}.cpp")
endforeach()
set(PERLIN_PROTOCOL_HEADERS "${PERLIN_PROTOCOL_OUT}/Types.hpp" "${PERLIN_PROTOCOL_OUT}/Packets.hpp" "${PERLIN_PROTOCOL_OUT}/Bridges.hpp")
list(APPEND PERLIN_PROTOCOL_SOURCES "${PERLIN_PROTOCOL_OUT}/Bridge.cpp")
set(PERLIN_PROTOCOL_LEGACY_ARGS)
set(PERLIN_PROTOCOL_LEGACY_SPECS)
foreach(entry IN LISTS PERLIN_PROTOCOL_LEGACY)
    string(REPLACE "=" ";" pair "${entry}")
    list(GET pair 0 legacy_protocol)
    list(GET pair 1 legacy_version)
    set(legacy_spec "${CMAKE_CURRENT_SOURCE_DIR}/protocol/legacy/bedrock-${legacy_version}.json")
    list(APPEND PERLIN_PROTOCOL_LEGACY_SPECS "${legacy_spec}")
    list(APPEND PERLIN_PROTOCOL_LEGACY_ARGS --legacy "${legacy_protocol}=${legacy_spec}")
    list(APPEND PERLIN_PROTOCOL_SOURCES "${PERLIN_PROTOCOL_OUT}/Bridge${legacy_protocol}.cpp")
    math(EXPR last_part "${PERLIN_PROTOCOL_LEGACY_PARTS} - 1")
    foreach(i RANGE 0 ${last_part})
        list(APPEND PERLIN_PROTOCOL_SOURCES "${PERLIN_PROTOCOL_OUT}/v${legacy_protocol}/Codec${i}.cpp")
    endforeach()
    list(APPEND PERLIN_PROTOCOL_HEADERS "${PERLIN_PROTOCOL_OUT}/v${legacy_protocol}/Types.hpp" "${PERLIN_PROTOCOL_OUT}/v${legacy_protocol}/Packets.hpp")
endforeach()
set(PERLIN_PROTOCOL_ROUNDTRIP "${PERLIN_PROTOCOL_OUT}/RoundTrip.cpp")
set(PERLIN_PROTOCOL_JSON "${PERLIN_PROTOCOL_OUT}/PacketJson.cpp")

add_custom_command(
    OUTPUT ${PERLIN_PROTOCOL_HEADERS} ${PERLIN_PROTOCOL_SOURCES} ${PERLIN_PROTOCOL_ROUNDTRIP} ${PERLIN_PROTOCOL_JSON}
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/protogen/protogen.py"
            --spec "${CMAKE_CURRENT_SOURCE_DIR}/protocol/bedrock-1.26.60.json"
            --overrides "${CMAKE_CURRENT_SOURCE_DIR}/protocol/overrides.json"
            --out "${PERLIN_PROTOCOL_OUT}"
            --parts ${PERLIN_PROTOCOL_CODEC_COUNT}
            --legacy-parts ${PERLIN_PROTOCOL_LEGACY_PARTS}
            --namespace proto${PERLIN_PROTOCOL_VERSION}
            --version ${PERLIN_PROTOCOL_VERSION}
            ${PERLIN_PROTOCOL_LEGACY_ARGS}
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/protogen/protogen.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/protocol/bedrock-1.26.60.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/protocol/overrides.json"
            ${PERLIN_PROTOCOL_LEGACY_SPECS}
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
