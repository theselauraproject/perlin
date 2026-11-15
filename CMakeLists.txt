cmake_minimum_required(VERSION 3.28)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/PerlinToolchain.cmake")
option(PERLIN_BUILD_TESTS "Build unit tests" OFF)

project(Perlin VERSION 1.0.0 LANGUAGES C CXX)
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/PerlinApiPackage.cmake")

set(THREADS_PREFER_PTHREAD_FLAG ON)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|arm64|aarch64|AARCH64)$" OR CMAKE_OSX_ARCHITECTURES MATCHES "^arm64$"
   OR CMAKE_CXX_COMPILER_TARGET MATCHES "^(aarch64|arm64)")
    set(PERLIN_TARGET_ARM64 ON)
else()
    set(PERLIN_TARGET_ARM64 OFF)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
    set(PERLIN_FREEBSD ON)
else()
    set(PERLIN_FREEBSD OFF)
endif()

if(EMSCRIPTEN OR PERLIN_IOS OR PERLIN_FREEBSD OR (WIN32 AND PERLIN_TARGET_ARM64))
    set(PERLIN_WASM3 ON)
else()
    set(PERLIN_WASM3 OFF)
endif()

set(PERLIN_BUNDLE_VERSION "${PROJECT_VERSION}" CACHE STRING "CFBundleShortVersionString / CFBundleVersion")

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
    foreach(root "$ENV{VCPKG_INSTALLATION_ROOT}" "$ENV{VCPKG_ROOT}")
        if(root AND EXISTS "${root}/installed/x64-windows-static/include/openssl")
            set(OPENSSL_ROOT_DIR "${root}/installed/x64-windows-static" CACHE PATH "OpenSSL root")
            break()
        endif()
    endforeach()
endif()

if(APPLE AND NOT PERLIN_IOS AND NOT OPENSSL_ROOT_DIR)
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

if(NOT DEFINED OPENSSL_USE_STATIC_LIBS)
    set(OPENSSL_USE_STATIC_LIBS ON)
endif()
if(WIN32)
    set(OPENSSL_MSVC_STATIC_RT ON)
endif()
if((EMSCRIPTEN OR ANDROID OR PERLIN_IOS) AND OPENSSL_ROOT_DIR)
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
include(cmake/PerlinWasm.cmake)
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

CPMAddPackage(NAME glslang GITHUB_REPOSITORY KhronosGroup/glslang GIT_TAG e2bb10631198bc9d73daf752d9170608d5e2c8bd GIT_SHALLOW NO DOWNLOAD_ONLY YES)
CPMAddPackage(NAME SPIRV-Cross GITHUB_REPOSITORY KhronosGroup/SPIRV-Cross GIT_TAG 300681bbcb8365a8d1eacdacdbdd0249bbf1d6b1 GIT_SHALLOW NO DOWNLOAD_ONLY YES)
option(PERLIN_SPIRV_OPT "Optimize converted java pack shaders with SPIRV-Tools" ON)
if(EMSCRIPTEN)
    set(PERLIN_SPIRV_OPT OFF)
endif()
if(PERLIN_SPIRV_OPT)
    CPMAddPackage(NAME SPIRV-Headers GITHUB_REPOSITORY KhronosGroup/SPIRV-Headers GIT_TAG 496543121ce6419f23d6fa5d7194ba66c36212d2 GIT_SHALLOW NO DOWNLOAD_ONLY YES)
    CPMAddPackage(NAME SPIRV-Tools GITHUB_REPOSITORY KhronosGroup/SPIRV-Tools GIT_TAG ef96ed763b43b59b33b31b362f09a02b729fa1c9 GIT_SHALLOW NO DOWNLOAD_ONLY YES)
endif()
include(cmake/PerlinJavaShaders.cmake)

include(cmake/PerlinGraphicsBackends.cmake)

include(cmake/PerlinConduit.cmake)
include(cmake/PerlinShaders.cmake)

set(PERLIN_GLM_DIR "${glm_SOURCE_DIR}" CACHE PATH "" FORCE)
add_subdirectory(src/render)
add_subdirectory(src/client/systems/renderer/gpu)

include(cmake/PerlinProtocol.cmake)

include(cmake/PerlinEmbed.cmake)

file(GLOB_RECURSE PERLIN_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
list(FILTER PERLIN_SOURCES EXCLUDE REGEX "/src/network/Nbt\\.cpp")
list(FILTER PERLIN_SOURCES EXCLUDE REGEX "/src/rhi/")
list(FILTER PERLIN_SOURCES EXCLUDE REGEX "/src/client/systems/renderer/(bgfx|shader)/")
list(FILTER PERLIN_SOURCES EXCLUDE REGEX "/src/client/systems/renderer/gpu/")
list(FILTER PERLIN_SOURCES EXCLUDE REGEX "/src/util/GraphicsDebugger\\.cpp$")
list(FILTER PERLIN_SOURCES EXCLUDE REGEX "/src/render/")
list(FILTER PERLIN_SOURCES EXCLUDE REGEX "/src/client/systems/javashaders/")
list(FILTER PERLIN_SOURCES EXCLUDE REGEX "/src/mods/")
list(FILTER PERLIN_SOURCES EXCLUDE REGEX "/src/(src|tests)/")
if(PERLIN_WASM3)
    list(APPEND PERLIN_SOURCES ${PERLIN_API_HOST_CORE_SOURCES} ${PERLIN_API_HOST_WASM3_SOURCES})
else()
    list(APPEND PERLIN_SOURCES ${PERLIN_API_HOST_CORE_SOURCES} ${PERLIN_API_HOST_WASMEDGE_SOURCES})
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
elseif(PERLIN_IOS)
    add_executable(Perlin MACOSX_BUNDLE ${PERLIN_SOURCES} ${PERLIN_EMBED_SOURCE} ${PERLIN_EMBED_NAMES_SOURCE})
    set_target_properties(Perlin PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/ios/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "Perlin"
        MACOSX_BUNDLE_GUI_IDENTIFIER "io.perlin.client"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PERLIN_BUNDLE_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PERLIN_BUNDLE_VERSION}")

    file(GLOB PERLIN_IOS_ICONS "${CMAKE_CURRENT_SOURCE_DIR}/packaging/ios/AppIcon*.png")
    set(PERLIN_IOS_BUNDLE_FILES ${PERLIN_IOS_ICONS})

    set(PERLIN_IOS_CA_BUNDLE "/etc/ssl/cert.pem" CACHE FILEPATH "PEM bundle of trusted root certificates shipped in the app")
    if(EXISTS "${PERLIN_IOS_CA_BUNDLE}")
        configure_file("${PERLIN_IOS_CA_BUNDLE}" "${CMAKE_CURRENT_BINARY_DIR}/ios/cacert.pem" COPYONLY)
        list(APPEND PERLIN_IOS_BUNDLE_FILES "${CMAKE_CURRENT_BINARY_DIR}/ios/cacert.pem")
    else()
        message(WARNING "PERLIN_IOS_CA_BUNDLE not found: HTTPS (sign-in, downloads) will fail certificate checks")
    endif()

    set(PERLIN_ASSET_MANIFEST "" CACHE FILEPATH "assets-manifest.json to ship in the app")
    if(PERLIN_ASSET_MANIFEST AND EXISTS "${PERLIN_ASSET_MANIFEST}")
        list(APPEND PERLIN_IOS_BUNDLE_FILES "${PERLIN_ASSET_MANIFEST}")
    endif()
    add_custom_command(TARGET Perlin POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${PERLIN_IOS_BUNDLE_FILES} "$<TARGET_BUNDLE_DIR:Perlin>"
        VERBATIM)
elseif(APPLE)
    set(PERLIN_ICNS "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/Perlin.icns")
    set_source_files_properties("${PERLIN_ICNS}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    add_executable(Perlin MACOSX_BUNDLE ${PERLIN_SOURCES} ${PERLIN_EMBED_SOURCE} ${PERLIN_EMBED_NAMES_SOURCE} "${PERLIN_ICNS}")
    set_target_properties(Perlin PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "Perlin"
        MACOSX_BUNDLE_GUI_IDENTIFIER "io.perlin.client"
        MACOSX_BUNDLE_ICON_FILE "Perlin.icns"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PERLIN_BUNDLE_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PERLIN_BUNDLE_VERSION}")
else()
    add_executable(Perlin ${PERLIN_SOURCES} ${PERLIN_EMBED_SOURCE} ${PERLIN_EMBED_NAMES_SOURCE})
endif()

target_compile_features(Perlin PRIVATE cxx_std_23)

set(PERLIN_BUILD_COMMIT "" CACHE STRING "git commit this build is made from (nightly builds)")
set(PERLIN_UPDATE_REPO "" CACHE STRING "owner/repo whose 'nightly' release the updater follows")
if(ANDROID)
    set(_perlin_asset "Perlin.apk")
elseif(PERLIN_IOS)
    set(_perlin_asset "Perlin.ipa")
elseif(EMSCRIPTEN)
    set(_perlin_asset "")
elseif(WIN32)
    if(PERLIN_TARGET_ARM64)
        set(_perlin_asset "Perlin-windows-arm64.zip")
    else()
        set(_perlin_asset "Perlin-windows-x64.zip")
    endif()
elseif(APPLE)
    if(PERLIN_TARGET_ARM64)
        set(_perlin_asset "Perlin-macos-arm64.zip")
    else()
        set(_perlin_asset "Perlin-macos-x64.zip")
    endif()
elseif(PERLIN_FREEBSD)
    set(_perlin_asset "Perlin-freebsd-x64.zip")
elseif(PERLIN_TARGET_ARM64)

    set(_perlin_asset "Perlin-linux-arm64.zip")
else()
    set(_perlin_asset "Perlin-linux-x64.zip")
endif()
set(PERLIN_UPDATE_ASSET "${_perlin_asset}" CACHE STRING "release asset this platform installs")
set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/client/services/update/Updater.cpp" APPEND PROPERTY COMPILE_DEFINITIONS
    "PERLIN_BUILD_COMMIT=\"${PERLIN_BUILD_COMMIT}\"" "PERLIN_UPDATE_REPO=\"${PERLIN_UPDATE_REPO}\""
    "PERLIN_UPDATE_ASSET=\"${PERLIN_UPDATE_ASSET}\"")

target_sources(Perlin PRIVATE ${PERLIN_PROTOCOL_JSON})
if(MSVC)
    set_source_files_properties(${PERLIN_PROTOCOL_JSON} PROPERTIES COMPILE_OPTIONS "/w;/bigobj")
else()
    set_source_files_properties(${PERLIN_PROTOCOL_JSON} PROPERTIES COMPILE_OPTIONS "-w")
endif()

target_include_directories(Perlin PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    ${PERLIN_API_INCLUDE_DIRS}
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
        "-sEXPORTED_FUNCTIONS=_main,_malloc,_free,_perlin_nn_on_message,_perlin_nn_on_closed,_perlin_web_file_picked,_perlin_web_version"
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
    target_link_libraries(Perlin PRIVATE ws2_32 crypt32 dbghelp advapi32 user32 gdi32 bcrypt iphlpapi winhttp)
elseif(PERLIN_IOS)

    set(PERLIN_IOS_OBJC "${CMAKE_CURRENT_SOURCE_DIR}/src/client/platform/ios/DocumentPicker.mm")
    target_sources(Perlin PRIVATE "${PERLIN_IOS_OBJC}")
    set_source_files_properties("${PERLIN_IOS_OBJC}" PROPERTIES LANGUAGE CXX COMPILE_OPTIONS "-x;objective-c++;-fobjc-arc")
    target_link_libraries(Perlin PRIVATE "-framework Security" "-framework CoreFoundation" "-framework Foundation"
        "-framework UIKit" "-framework QuartzCore" "-framework Metal" "-framework GameController" "-framework AVFoundation"
        "-framework UniformTypeIdentifiers")
elseif(APPLE)
    target_link_libraries(Perlin PRIVATE "-framework Security" "-framework CoreFoundation")
    target_compile_definitions(Perlin PRIVATE CPPHTTPLIB_USE_CERTS_FROM_MACOSX_KEYCHAIN)
elseif(ANDROID)
    target_link_libraries(Perlin PRIVATE log android)
    target_link_options(Perlin PRIVATE -Wl,--build-id=none -Wl,-z,max-page-size=16384)
elseif(NOT EMSCRIPTEN)
    target_link_libraries(Perlin PRIVATE ${CMAKE_DL_LIBS})
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_options(Perlin PRIVATE -static-libstdc++ -static-libgcc)
    endif()
endif()

option(PERLIN_DEBUG_OPT "Optimise Perlin's own sources in Debug builds" ON)
if(PERLIN_DEBUG_OPT)
    if(MSVC)
        set_source_files_properties(${PERLIN_SOURCES} PROPERTIES COMPILE_OPTIONS "$<$<CONFIG:Debug>:/O2;/Ob2>")
    else()
        set_source_files_properties(${PERLIN_SOURCES} PROPERTIES COMPILE_OPTIONS "$<$<CONFIG:Debug>:-O2>")
    endif()
endif()

foreach(PERLIN_PHYSICS_SOURCE IN ITEMS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/world/movement/PlayerPhysics.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/world/movement/VehiclePhysics.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/client/systems/player/LocalPlayer.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/client/systems/player/PlayerFov.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/client/systems/resources/BlockOffsetDefinition.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/client/systems/resources/BlockBoxDefinition.cpp")
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
        "${CMAKE_CURRENT_SOURCE_DIR}/src/client/systems/renderer/chunk/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/client/systems/multiplayer/*.cpp"
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

if(MSVC AND NOT PERLIN_CLANG_CL_BUILD)
    set_source_files_properties(src/client/systems/player/HeldBuild.cpp PROPERTIES COMPILE_OPTIONS "/fp:strict")
else()
    set_source_files_properties(src/client/systems/player/HeldBuild.cpp PROPERTIES COMPILE_OPTIONS "-fno-fast-math;-ffp-contract=off")
endif()

include(CheckIPOSupported)
if(EMSCRIPTEN OR ANDROID OR PERLIN_IOS)
    set(PERLIN_IPO OFF)
else()
    check_ipo_supported(RESULT PERLIN_IPO OUTPUT PERLIN_IPO_LOG)
endif()
if(PERLIN_IPO)
    set_property(TARGET Perlin PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        set_source_files_properties(src/client/systems/entity/EntityRenderer.cpp PROPERTIES COMPILE_OPTIONS "-fno-lto")
    elseif(MSVC)
        set_source_files_properties(src/client/systems/entity/EntityRenderer.cpp PROPERTIES COMPILE_OPTIONS "/GL-")
    endif()
endif()

if(NOT ANDROID)
    install(TARGETS Perlin RUNTIME DESTINATION . BUNDLE DESTINATION .)
endif()
if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)
    install(FILES packaging/linux/io.perlin.client.desktop DESTINATION share/applications)
    install(DIRECTORY packaging/linux/icons/ DESTINATION share/icons/hicolor)
endif()

option(PERLIN_LIBAV "Link FFmpeg libraries into Perlin for built-in video encoding" ON)

option(PERLIN_WINDOWS_FFMPEG "Link/bundle FFmpeg on Windows" OFF)
if(WIN32 AND NOT PERLIN_WINDOWS_FFMPEG)
    set(PERLIN_LIBAV OFF)
    set(PERLIN_BUNDLE_FFMPEG OFF CACHE BOOL "" FORCE)
endif()
set(PERLIN_LIBAV_FOUND OFF)
if(PERLIN_LIBAV AND NOT EMSCRIPTEN AND NOT ANDROID AND NOT PERLIN_IOS)
    set(FFMPEG_ROOT "" CACHE PATH "Prefix containing include/libavcodec and lib/ with static FFmpeg libraries")
    if(FFMPEG_ROOT AND EXISTS "${FFMPEG_ROOT}/include/libavcodec/avcodec.h")
        file(GLOB _av_libs "${FFMPEG_ROOT}/lib/*.lib" "${FFMPEG_ROOT}/lib/*.a")

        if(_av_libs)
            target_include_directories(Perlin PRIVATE "${FFMPEG_ROOT}/include")
            target_link_libraries(Perlin PRIVATE ${_av_libs})
            set(PERLIN_LIBAV_FOUND ON)
        endif()
    elseif(NOT WIN32 AND NOT APPLE)

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

if(WIN32 AND NOT EMSCRIPTEN AND NOT ANDROID AND NOT PERLIN_LIBAV_FOUND)
    option(PERLIN_BUNDLE_FFMPEG "Download an LGPL ffmpeg build and ship it next to perlin.exe" ON)

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
if(PERLIN_BUILD_TESTS AND NOT EMSCRIPTEN AND NOT ANDROID AND NOT PERLIN_IOS)
    enable_testing()
    add_executable(skin_lifetime_selftest tests/skin_lifetime_selftest.cpp src/client/systems/model/EntityTracker.cpp)
    target_include_directories(skin_lifetime_selftest PRIVATE src "${glm_SOURCE_DIR}")
    target_compile_features(skin_lifetime_selftest PRIVATE cxx_std_23)
    add_test(NAME skin_lifetime_selftest COMMAND skin_lifetime_selftest)
    add_executable(strings_selftest tests/strings_selftest.cpp)
    target_include_directories(strings_selftest PRIVATE src)
    target_compile_features(strings_selftest PRIVATE cxx_std_23)
    add_test(NAME strings_selftest COMMAND strings_selftest)
    add_executable(util_selftest tests/util_selftest.cpp src/util/JsonValue.cpp)
    target_include_directories(util_selftest PRIVATE src)
    target_compile_features(util_selftest PRIVATE cxx_std_23)
    add_test(NAME util_selftest COMMAND util_selftest)
    add_executable(task_group_selftest tests/task_group_selftest.cpp)
    target_include_directories(task_group_selftest PRIVATE src)
    target_compile_features(task_group_selftest PRIVATE cxx_std_23)
    target_link_libraries(task_group_selftest PRIVATE Threads::Threads)
    add_test(NAME task_group_selftest COMMAND task_group_selftest)
    add_executable(java_program_cache_selftest tests/java_program_cache_selftest.cpp
        src/client/systems/renderer/gpu/JavaProgramCache.cpp)
    target_include_directories(java_program_cache_selftest PRIVATE src "${glm_SOURCE_DIR}")
    target_compile_features(java_program_cache_selftest PRIVATE cxx_std_23)
    add_test(NAME java_program_cache_selftest COMMAND java_program_cache_selftest)
    add_executable(websocket_selftest tests/websocket_selftest.cpp src/util/WebSocket.cpp src/util/ModSocket.cpp)
    target_include_directories(websocket_selftest PRIVATE src)
    target_compile_features(websocket_selftest PRIVATE cxx_std_23)
    target_link_libraries(websocket_selftest PRIVATE Threads::Threads)
    if(WIN32)
        target_link_libraries(websocket_selftest PRIVATE ws2_32)
    endif()
    add_test(NAME websocket_selftest COMMAND websocket_selftest)
    set_tests_properties(websocket_selftest PROPERTIES TIMEOUT 120)
    add_executable(utc_time_selftest tests/utc_time_selftest.cpp)
    target_include_directories(utc_time_selftest PRIVATE src)
    target_compile_features(utc_time_selftest PRIVATE cxx_std_23)
    add_test(NAME utc_time_selftest COMMAND utc_time_selftest)
    add_executable(mod_support_selftest tests/mod_support_selftest.cpp src/client/systems/mods/ModSupport.cpp)
    target_include_directories(mod_support_selftest PRIVATE src ${PERLIN_API_INCLUDE_DIRS})
    target_compile_features(mod_support_selftest PRIVATE cxx_std_23)
    add_test(NAME mod_support_selftest COMMAND mod_support_selftest)
    add_executable(actor_lerp_selftest tests/actor_lerp_selftest.cpp src/client/systems/model/EntityTracker.cpp)
    target_include_directories(actor_lerp_selftest PRIVATE src "${glm_SOURCE_DIR}")
    target_compile_features(actor_lerp_selftest PRIVATE cxx_std_23)
    add_test(NAME actor_lerp_selftest COMMAND actor_lerp_selftest)
    add_executable(block_palette_selftest tests/block_palette_selftest.cpp
        src/world/level/block/BlockPalette.cpp src/util/Json.cpp)
    target_include_directories(block_palette_selftest PRIVATE src)
    target_link_libraries(block_palette_selftest PRIVATE glaze::glaze)
    target_compile_features(block_palette_selftest PRIVATE cxx_std_23)
    add_test(NAME block_palette_selftest COMMAND block_palette_selftest "${CMAKE_CURRENT_SOURCE_DIR}/resources/data/blocks/block_states.json" "${CMAKE_CURRENT_SOURCE_DIR}/resources/data/blocks/block_versions.json")
    add_executable(entity_render_selftest tests/entity_render_selftest.cpp
        src/client/systems/entity/EntityQuery.cpp src/client/systems/model/EntityTracker.cpp src/client/systems/entity/BlockEntityView.cpp
        src/client/systems/model/BlockEntities.cpp src/client/systems/attachable/Geometry.cpp
        src/client/systems/attachable/Animation.cpp src/client/systems/attachable/Molang.cpp src/util/JsonValue.cpp
        src/world/level/block/BlockPalette.cpp src/util/Json.cpp)
    target_include_directories(entity_render_selftest PRIVATE src "${glm_SOURCE_DIR}")
    target_link_libraries(entity_render_selftest PRIVATE glaze::glaze)
    target_compile_features(entity_render_selftest PRIVATE cxx_std_23)
    add_test(NAME entity_render_selftest COMMAND entity_render_selftest)
    add_executable(player_fov_selftest tests/player_fov_selftest.cpp src/client/systems/player/PlayerFov.cpp)
    target_include_directories(player_fov_selftest PRIVATE src)
    target_compile_features(player_fov_selftest PRIVATE cxx_std_23)
    add_test(NAME player_fov_selftest COMMAND player_fov_selftest)
    add_executable(panorama_blur_selftest tests/panorama_blur_selftest.cpp src/client/ui/TitlePanorama.cpp)
    target_include_directories(panorama_blur_selftest PRIVATE src)
    target_compile_features(panorama_blur_selftest PRIVATE cxx_std_23)
    add_test(NAME panorama_blur_selftest COMMAND panorama_blur_selftest)
    add_executable(format_codes_selftest tests/format_codes_selftest.cpp src/util/JsonValue.cpp src/client/ui/UiExpr.cpp
        src/client/ui/JsonUi.cpp src/client/ui/UiLayout.cpp src/client/systems/text/McFont.cpp src/client/systems/text/FontProviderTables.cpp src/client/ui/TenFont.cpp
        src/client/ui/SceneEmit.cpp src/client/ui/Scene.cpp src/client/ui/UiTemplate.cpp src/client/systems/text/FormatCodes.cpp src/client/ui/HudData.cpp src/client/systems/model/EffectData.cpp
        src/client/systems/i18n/Lang.cpp src/client/systems/resources/ServerPackRegistry.cpp src/client/systems/i18n/BuiltinEnglish.cpp
        src/util/Resources.cpp src/util/Paths.cpp)
    target_include_directories(format_codes_selftest PRIVATE src "${stb_SOURCE_DIR}")
    target_compile_features(format_codes_selftest PRIVATE cxx_std_23)
    add_test(NAME format_codes_selftest COMMAND format_codes_selftest)
    add_executable(block_box_definition_selftest tests/block_box_definition_selftest.cpp
        src/client/systems/resources/BlockBoxDefinition.cpp src/util/JsonValue.cpp)
    target_include_directories(block_box_definition_selftest PRIVATE src "${glm_SOURCE_DIR}")
    target_compile_features(block_box_definition_selftest PRIVATE cxx_std_23)
    add_test(NAME block_box_definition_selftest COMMAND block_box_definition_selftest)
    add_executable(hud_dump tests/hud_dump.cpp src/util/JsonValue.cpp src/client/ui/UiExpr.cpp src/client/ui/JsonUi.cpp
        src/client/ui/UiLayout.cpp src/client/systems/text/McFont.cpp src/client/systems/text/FontProviderTables.cpp src/client/ui/TenFont.cpp src/client/ui/SceneEmit.cpp src/client/ui/Scene.cpp src/client/ui/UiTemplate.cpp src/client/systems/text/FormatCodes.cpp src/client/ui/HudData.cpp
        src/client/ui/HudRenderers.cpp src/client/systems/model/EffectData.cpp src/client/systems/i18n/Lang.cpp src/client/systems/resources/ServerPackRegistry.cpp src/client/systems/i18n/BuiltinEnglish.cpp src/util/Resources.cpp src/util/Paths.cpp)
    target_include_directories(hud_dump PRIVATE src "${stb_SOURCE_DIR}")
    target_compile_features(hud_dump PRIVATE cxx_std_23)
    add_test(NAME hud_boss_bindings COMMAND hud_dump --boss-selftest)
    add_executable(screen_dump tests/screen_dump.cpp src/util/JsonValue.cpp src/client/ui/UiExpr.cpp src/client/ui/JsonUi.cpp
        src/client/ui/UiLayout.cpp src/client/systems/text/McFont.cpp src/client/systems/text/FontProviderTables.cpp src/client/ui/TenFont.cpp src/client/ui/SceneEmit.cpp src/client/ui/Scene.cpp src/client/ui/UiTemplate.cpp src/client/systems/text/FormatCodes.cpp src/client/ui/HudData.cpp src/client/systems/model/EffectData.cpp
        src/client/systems/i18n/Lang.cpp src/client/systems/resources/ServerPackRegistry.cpp src/client/systems/i18n/BuiltinEnglish.cpp src/util/Resources.cpp src/util/Paths.cpp)
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
        PASS_REGULAR_EXPRESSION "alpha=0[.]30*")
    add_test(NAME ui_repeated_routes COMMAND screen_dump routes.routing_screen 320 240
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ui_routes")
    set_tests_properties(ui_repeated_routes PROPERTIES ENVIRONMENT "SCREEN_TREE=1;SCREEN_VARS={\"#title_text\":\"middle\"}"
        PASS_REGULAR_EXPRESSION "text=EXPECTED_MIDDLE_ROUTE"
        FAIL_REGULAR_EXPRESSION "text=WRONG_FIRST_ROUTE;text=WRONG_LAST_ROUTE")
    add_test(NAME ui_fractional_font COMMAND screen_dump font_sizes.fractional_screen 320 240
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ui_font_sizes")
    set_tests_properties(ui_fractional_font PROPERTIES ENVIRONMENT "SCREEN_RML=1"
        PASS_REGULAR_EXPRESSION "font=minecraft-size-6[.]4")
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
        src/client/systems/gfx/Tessellator.cpp src/client/systems/gfx/SkyGeometry.cpp src/client/systems/gfx/MaterialDb.cpp
        src/util/JsonValue.cpp src/util/Paths.cpp src/util/Resources.cpp)
    target_sources(gpu_level_unload_selftest PRIVATE
        src/client/systems/javashaders/PackSource.cpp src/client/systems/javashaders/PackSourceZip.cpp
        src/client/systems/javashaders/Properties.cpp src/client/systems/javashaders/Cond.cpp
        src/client/systems/resources/TextureLoader.cpp src/world/level/block/BlockPalette.cpp src/util/Json.cpp)
    target_include_directories(gpu_level_unload_selftest PRIVATE "${stb_SOURCE_DIR}")
    target_link_libraries(gpu_level_unload_selftest PRIVATE glaze::glaze miniz)
    target_include_directories(gpu_level_unload_selftest BEFORE PRIVATE tests/stubs src "${glm_SOURCE_DIR}")
    target_link_libraries(gpu_level_unload_selftest PRIVATE perlin_gpu)
    add_test(NAME gpu_level_unload_selftest COMMAND gpu_level_unload_selftest "${PERLIN_SHADER_PACK}")
    add_executable(ui_batch_selftest tests/ui_batch_selftest.cpp tests/gpu_context_stubs.cpp
        src/client/systems/gfx/Tessellator.cpp src/client/systems/gfx/SkyGeometry.cpp src/client/systems/gfx/MaterialDb.cpp
        src/util/JsonValue.cpp src/util/Paths.cpp src/util/Resources.cpp
        src/client/systems/javashaders/PackSource.cpp src/client/systems/javashaders/PackSourceZip.cpp
        src/client/systems/javashaders/Properties.cpp src/client/systems/javashaders/Cond.cpp
        src/client/systems/resources/TextureLoader.cpp src/world/level/block/BlockPalette.cpp src/util/Json.cpp)
    target_include_directories(ui_batch_selftest PRIVATE "${stb_SOURCE_DIR}")
    target_link_libraries(ui_batch_selftest PRIVATE glaze::glaze miniz perlin_gpu)
    target_include_directories(ui_batch_selftest BEFORE PRIVATE tests/stubs src "${glm_SOURCE_DIR}")
    add_test(NAME ui_batch_selftest COMMAND ui_batch_selftest "${PERLIN_SHADER_PACK}")
    add_executable(render_tuner_selftest tests/render_tuner_selftest.cpp src/client/systems/renderer/gpu/RenderTuner.cpp)
    target_link_libraries(render_tuner_selftest PRIVATE perlin_gpu)
    target_include_directories(render_tuner_selftest BEFORE PRIVATE src)
    add_test(NAME render_tuner_selftest COMMAND render_tuner_selftest)
    add_test(NAME ui_batch_fallback_selftest COMMAND ui_batch_selftest "${PERLIN_SHADER_PACK}")
    set_tests_properties(ui_batch_fallback_selftest PROPERTIES ENVIRONMENT "PERLIN_NO_GPU_UI=1")
    set_tests_properties(ui_batch_selftest ui_batch_fallback_selftest PROPERTIES SKIP_RETURN_CODE 77)
    add_executable(gpu_terrain_selftest tests/gpu_terrain_selftest.cpp src/client/systems/gfx/Tessellator.cpp)
    target_include_directories(gpu_terrain_selftest BEFORE PRIVATE tests/stubs src "${glm_SOURCE_DIR}")
    target_link_libraries(gpu_terrain_selftest PRIVATE perlin_gpu)
    add_test(NAME gpu_terrain_selftest COMMAND gpu_terrain_selftest "${PERLIN_SHADER_PACK}")
    set_tests_properties(gpu_terrain_selftest PROPERTIES SKIP_RETURN_CODE 77)
    include(CheckCXXSourceCompiles)
    set(_perlin_saved_flags "${CMAKE_REQUIRED_FLAGS}")
    if(MSVC)
        set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} /std:c++latest")
    else()
        set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -std=c++23")
    endif()
    check_cxx_source_compiles("#include <stdfloat>\nint main() { return 0; }" PERLIN_HAVE_STDFLOAT)
    set(CMAKE_REQUIRED_FLAGS "${_perlin_saved_flags}")
    if(PERLIN_SLANGC AND PERLIN_HAVE_STDFLOAT)
        set(_cull_cpu "${CMAKE_CURRENT_BINARY_DIR}/generated/terrain_cull_cpu.cpp")
        add_custom_command(OUTPUT "${_cull_cpu}"
            COMMAND "${PERLIN_SLANGC}" "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/slang/programs/terrain_cull.slang"
                    -target cpp -entry csMain -stage compute -o "${_cull_cpu}"
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/slang/programs/terrain_cull.slang"
            COMMENT "Compiling terrain_cull.slang for the cpu" VERBATIM)
        add_executable(terrain_cull_cpu_selftest tests/terrain_cull_cpu_selftest.cpp src/client/systems/gfx/Tessellator.cpp
            "${_cull_cpu}")
        set_source_files_properties("${_cull_cpu}" PROPERTIES HEADER_FILE_ONLY ON)
        target_include_directories(terrain_cull_cpu_selftest BEFORE PRIVATE tests/stubs src "${glm_SOURCE_DIR}"
            "${CMAKE_CURRENT_BINARY_DIR}/generated")
        target_compile_options(terrain_cull_cpu_selftest PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang>:-w>)
        target_link_libraries(terrain_cull_cpu_selftest PRIVATE perlin_gpu)
        add_test(NAME terrain_cull_cpu_selftest COMMAND terrain_cull_cpu_selftest "${PERLIN_SHADER_PACK}")
        set_tests_properties(terrain_cull_cpu_selftest PROPERTIES SKIP_RETURN_CODE 77)
    endif()
    add_executable(render_bench tools/render_bench.cpp tests/gpu_context_stubs.cpp
        src/client/systems/gfx/Tessellator.cpp src/client/systems/gfx/SkyGeometry.cpp src/client/systems/gfx/MaterialDb.cpp
        src/util/JsonValue.cpp src/util/Paths.cpp src/util/Resources.cpp
        src/client/systems/javashaders/PackSource.cpp src/client/systems/javashaders/PackSourceZip.cpp
        src/client/systems/javashaders/Properties.cpp src/client/systems/javashaders/Cond.cpp
        src/client/systems/resources/TextureLoader.cpp src/world/level/block/BlockPalette.cpp src/util/Json.cpp)
    target_include_directories(render_bench PRIVATE "${stb_SOURCE_DIR}")
    target_link_libraries(render_bench PRIVATE glaze::glaze miniz perlin_gpu)
    target_include_directories(render_bench BEFORE PRIVATE tests/stubs src "${glm_SOURCE_DIR}")
    add_executable(mod_gpu_selftest tests/mod_gpu_selftest.cpp)
    target_include_directories(mod_gpu_selftest PRIVATE src ${PERLIN_API_INCLUDE_DIRS})
    target_link_libraries(mod_gpu_selftest PRIVATE perlin_gpu)
    add_test(NAME mod_gpu_selftest COMMAND mod_gpu_selftest)
    set_tests_properties(gpu_level_unload_selftest PROPERTIES SKIP_RETURN_CODE 77)
    add_test(NAME java_to_slang_selftest COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/java_to_slang_selftest.py")
    add_executable(block_states_selftest tests/block_states_selftest.cpp src/client/systems/javashaders/BlockStates.cpp)
    target_include_directories(block_states_selftest PRIVATE src)
    add_test(NAME block_states_selftest COMMAND block_states_selftest)
    add_executable(benchmark_selftest tests/benchmark_selftest.cpp src/client/systems/diagnostics/Benchmark.cpp)
    target_include_directories(benchmark_selftest PRIVATE src)
    add_test(NAME benchmark_selftest COMMAND benchmark_selftest)

    target_link_libraries(gpu_level_unload_selftest PRIVATE Threads::Threads)
    option(PERLIN_BUILD_PACK_BENCH "Build tools/java_pack_bench: times a java shader pack headless" OFF)
    if(PERLIN_BUILD_PACK_BENCH)
        add_executable(java_pack_bench tools/java_pack_bench.cpp src/client/systems/gfx/Tessellator.cpp
            tests/gpu_context_stubs.cpp src/util/JsonValue.cpp src/util/Paths.cpp src/util/Resources.cpp
            src/client/systems/javashaders/PackSource.cpp src/client/systems/javashaders/PackSourceZip.cpp
            src/client/systems/javashaders/Properties.cpp src/client/systems/javashaders/Cond.cpp
            src/client/systems/resources/TextureLoader.cpp src/world/level/block/BlockPalette.cpp src/util/Json.cpp)
        target_include_directories(java_pack_bench PRIVATE "${stb_SOURCE_DIR}")
        target_include_directories(java_pack_bench BEFORE PRIVATE tests/stubs src "${glm_SOURCE_DIR}")
        target_link_libraries(java_pack_bench PRIVATE perlin_gpu glaze::glaze miniz)
    endif()

    add_executable(jshader_check tools/jshader_check.cpp)
    target_include_directories(jshader_check PRIVATE src)
    target_link_libraries(jshader_check PRIVATE perlin_gpu)

    add_executable(ten_font_metrics_selftest tests/ten_font_metrics_selftest.cpp
        src/client/ui/TenFont.cpp src/client/systems/text/McFont.cpp src/client/systems/text/FontProviderTables.cpp src/util/JsonValue.cpp src/util/Resources.cpp src/util/Paths.cpp)
    target_include_directories(ten_font_metrics_selftest PRIVATE src "${stb_SOURCE_DIR}")
    target_compile_features(ten_font_metrics_selftest PRIVATE cxx_std_23)
    add_test(NAME ten_font_metrics_selftest COMMAND ten_font_metrics_selftest)
    set_tests_properties(ten_font_metrics_selftest PROPERTIES SKIP_RETURN_CODE 77)
    add_executable(discord_service_difftest tests/discord_service_difftest.cpp
        src/client/services/discord/DiscordService.cpp src/client/services/discord/DiscordRpc.cpp
        src/util/UrlHandlerRegistration.cpp src/util/Paths.cpp src/util/Json.cpp src/util/JsonValue.cpp)
    target_include_directories(discord_service_difftest PRIVATE src)
    target_compile_features(discord_service_difftest PRIVATE cxx_std_23)
    find_package(Threads REQUIRED)
    target_link_libraries(discord_service_difftest PRIVATE Threads::Threads glaze::glaze)
    add_test(NAME discord_service_difftest COMMAND discord_service_difftest)
    add_executable(benchmark_driver_difftest tests/benchmark_driver_difftest.cpp
        src/client/systems/diagnostics/BenchmarkDriver.cpp src/client/systems/diagnostics/Benchmark.cpp)
    target_include_directories(benchmark_driver_difftest PRIVATE src)
    target_compile_features(benchmark_driver_difftest PRIVATE cxx_std_23)
    add_test(NAME benchmark_driver_difftest COMMAND benchmark_driver_difftest)
    add_executable(movement_sounds_difftest tests/movement_sounds_difftest.cpp
        src/client/systems/audio/MovementSounds.cpp)
    target_include_directories(movement_sounds_difftest PRIVATE src "${glm_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_compile_features(movement_sounds_difftest PRIVATE cxx_std_23)
    add_test(NAME movement_sounds_difftest COMMAND movement_sounds_difftest)
    add_executable(account_service_test tests/account_service_test.cpp
        src/client/services/account/AccountService.cpp src/client/services/account/AccountStore.cpp
        src/util/Json.cpp src/util/JsonValue.cpp)
    target_include_directories(account_service_test PRIVATE src)
    target_compile_features(account_service_test PRIVATE cxx_std_23)
    target_link_libraries(account_service_test PRIVATE Threads::Threads glaze::glaze)
    add_test(NAME account_service_test COMMAND account_service_test)
    add_executable(block_entity_meshes_difftest tests/block_entity_meshes_difftest.cpp
        src/client/systems/entity/BlockEntityMeshes.cpp src/client/systems/text/McFont.cpp src/client/systems/text/FontProviderTables.cpp src/util/JsonValue.cpp src/util/Resources.cpp src/util/Paths.cpp src/client/systems/text/FormatCodes.cpp)
    target_include_directories(block_entity_meshes_difftest PRIVATE src "${stb_SOURCE_DIR}" "${glm_SOURCE_DIR}")
    target_link_libraries(block_entity_meshes_difftest PRIVATE glm::glm)
    target_compile_features(block_entity_meshes_difftest PRIVATE cxx_std_23)
    add_test(NAME block_entity_meshes_difftest COMMAND block_entity_meshes_difftest)
    add_executable(font_cache_selftest tests/font_cache_selftest.cpp src/client/systems/text/McFont.cpp src/client/systems/text/FontProviderTables.cpp src/util/JsonValue.cpp src/util/Resources.cpp src/util/Paths.cpp)
    target_include_directories(font_cache_selftest PRIVATE src "${stb_SOURCE_DIR}")
    target_compile_features(font_cache_selftest PRIVATE cxx_std_23)
    add_test(NAME font_cache_selftest COMMAND font_cache_selftest)
    add_executable(ui_library_selftest tests/ui_library_selftest.cpp src/util/JsonValue.cpp
        src/client/ui/UiExpr.cpp src/client/ui/JsonUi.cpp)
    target_include_directories(ui_library_selftest PRIVATE src)
    target_compile_features(ui_library_selftest PRIVATE cxx_std_23)
    add_test(NAME ui_library_selftest COMMAND ui_library_selftest)
    add_executable(ui_conformance tests/ui_conformance.cpp src/util/JsonValue.cpp src/client/ui/UiExpr.cpp src/client/ui/JsonUi.cpp
        src/client/ui/UiLayout.cpp src/client/systems/text/McFont.cpp src/client/systems/text/FontProviderTables.cpp src/client/ui/TenFont.cpp src/client/ui/SceneEmit.cpp src/client/ui/Scene.cpp src/client/ui/UiTemplate.cpp
        src/client/ui/HudData.cpp src/client/systems/text/FormatCodes.cpp src/client/systems/i18n/Lang.cpp
        src/client/systems/i18n/BuiltinEnglish.cpp src/client/systems/resources/ServerPackRegistry.cpp src/util/Resources.cpp src/util/Paths.cpp)
    target_include_directories(ui_conformance PRIVATE src "${stb_SOURCE_DIR}" "${glm_SOURCE_DIR}")
    target_compile_features(ui_conformance PRIVATE cxx_std_23)
    add_test(NAME ui_screen_discovery COMMAND ui_conformance
        "regression:${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ui_deferred")
    set_tests_properties(ui_screen_discovery PROPERTIES
        PASS_REGULAR_EXPRESSION "screens=2 +built=2 +failed=0 +missing=0")
    add_executable(block_classify_dump
        tests/block_classify_dump.cpp
        src/client/systems/player/BlockCollision.cpp
        src/world/level/block/BlockPalette.cpp
        src/util/Json.cpp
        src/util/Resources.cpp
        src/util/Paths.cpp
        src/world/movement/VoxelShapes.cpp
        src/world/movement/PlayerPhysics.cpp
        src/client/systems/resources/ServerBlocks.cpp
        src/client/systems/resources/BlockBoxDefinition.cpp
        src/client/systems/resources/BlockOffsetDefinition.cpp
        src/util/JsonValue.cpp
        src/world/level/Level.cpp
        src/world/level/chunk/ChunkCodec.cpp
        src/world/level/lighting/LightRegistry.cpp
        src/client/systems/blocks/BlockGeometry.cpp)
    target_include_directories(block_classify_dump PRIVATE src "${glm_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_link_libraries(block_classify_dump PRIVATE glaze::glaze Threads::Threads)
    target_compile_features(block_classify_dump PRIVATE cxx_std_23)
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/block_classify_empty")
    add_test(NAME block_classify_registry
        COMMAND block_classify_dump "${CMAKE_SOURCE_DIR}/resources/data/blocks/block_states.json" "${CMAKE_SOURCE_DIR}/resources"
                "${CMAKE_BINARY_DIR}/block_classify_empty" "${CMAKE_SOURCE_DIR}/tests/block_classify_registry_golden.txt")
    add_test(NAME block_classify_fallback
        COMMAND block_classify_dump "${CMAKE_SOURCE_DIR}/resources/data/blocks/block_states.json" "${CMAKE_BINARY_DIR}/block_classify_empty"
                "${CMAKE_BINARY_DIR}/block_classify_empty" "${CMAKE_SOURCE_DIR}/tests/block_classify_fallback_golden.txt"
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/block_classify_empty")

    add_executable(targeting_selftest tests/targeting_selftest.cpp)
    target_include_directories(targeting_selftest PRIVATE src "${glm_SOURCE_DIR}")
    target_compile_features(targeting_selftest PRIVATE cxx_std_23)
    add_test(NAME targeting_selftest COMMAND targeting_selftest)
    add_executable(hand_anim_selftest tests/hand_anim_selftest.cpp)
    target_include_directories(hand_anim_selftest PRIVATE src)
    target_compile_features(hand_anim_selftest PRIVATE cxx_std_23)
    add_test(NAME hand_anim_selftest COMMAND hand_anim_selftest)
    add_executable(custom_sky_selftest tests/custom_sky_selftest.cpp src/client/systems/renderer/CustomSky.cpp
        src/client/systems/gfx/SkyGeometry.cpp)
    target_include_directories(custom_sky_selftest PRIVATE src "${glm_SOURCE_DIR}")
    target_compile_features(custom_sky_selftest PRIVATE cxx_std_23)
    add_test(NAME custom_sky_selftest COMMAND custom_sky_selftest)
    add_executable(heldbuild_selftest
        tests/heldbuild_selftest.cpp
        src/client/systems/player/HeldBuild.cpp
        src/world/movement/PlayerPhysics.cpp
        src/client/systems/blocks/BlockUse.cpp)
    target_include_directories(heldbuild_selftest PRIVATE src "${glm_SOURCE_DIR}")
    target_compile_features(heldbuild_selftest PRIVATE cxx_std_23)
    add_test(NAME heldbuild_selftest COMMAND heldbuild_selftest)
    add_executable(entity_animation_selftest tests/entity_animation_selftest.cpp
        src/client/systems/entity/JavaAnimation.cpp src/client/systems/attachable/Geometry.cpp
        src/client/systems/attachable/Animation.cpp src/client/systems/attachable/Molang.cpp src/util/JsonValue.cpp)
    target_include_directories(entity_animation_selftest PRIVATE src "${glm_SOURCE_DIR}")
    target_compile_features(entity_animation_selftest PRIVATE cxx_std_23)
    add_test(NAME entity_animation_selftest COMMAND entity_animation_selftest
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/java_animation_reference.txt"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/java_animation_root_reference.txt"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/java17_animation_reference.txt")
    add_executable(attachable_selftest tests/attachable_selftest.cpp
        src/client/systems/attachable/Molang.cpp src/client/systems/attachable/Animation.cpp src/client/systems/attachable/Geometry.cpp
        src/util/JsonValue.cpp)
    target_include_directories(attachable_selftest PRIVATE src "${glm_SOURCE_DIR}")
    target_compile_features(attachable_selftest PRIVATE cxx_std_23)
    add_test(NAME attachable_selftest COMMAND attachable_selftest)

    add_executable(particle_selftest tests/particle_selftest.cpp src/client/systems/particle/DataParticles.cpp
        src/client/systems/attachable/Molang.cpp src/util/JsonValue.cpp)
    target_include_directories(particle_selftest PRIVATE src "${glm_SOURCE_DIR}")
    target_compile_features(particle_selftest PRIVATE cxx_std_23)
    add_test(NAME particle_selftest COMMAND particle_selftest)

    add_executable(audio_selftest tests/audio_selftest.cpp src/client/systems/audio/AudioEngine.cpp src/client/systems/audio/Decode.cpp
        src/client/systems/audio/SoundSystem.cpp src/client/systems/audio/Subtitles.cpp src/util/JsonValue.cpp
        src/client/systems/i18n/Lang.cpp src/client/systems/resources/ServerPackRegistry.cpp src/client/systems/i18n/BuiltinEnglish.cpp src/util/Resources.cpp src/util/Paths.cpp)
    target_compile_definitions(audio_selftest PRIVATE PERLIN_LANG_FILE="${CMAKE_SOURCE_DIR}/resources/localization/perlin_en_US.lang")
    target_include_directories(audio_selftest PRIVATE src "${glm_SOURCE_DIR}" "${stb_SOURCE_DIR}")
    target_link_libraries(audio_selftest PRIVATE SDL3::SDL3)
    target_compile_features(audio_selftest PRIVATE cxx_std_23)
    add_test(NAME audio_selftest COMMAND audio_selftest)

    add_executable(lang_selftest tests/lang_selftest.cpp src/client/systems/i18n/Lang.cpp src/client/systems/resources/ServerPackRegistry.cpp src/client/systems/i18n/BuiltinEnglish.cpp src/util/JsonValue.cpp src/util/Resources.cpp src/util/Paths.cpp)
    target_include_directories(lang_selftest PRIVATE src)
    target_compile_features(lang_selftest PRIVATE cxx_std_23)
    add_test(NAME lang_selftest COMMAND lang_selftest "${CMAKE_SOURCE_DIR}/resources")

    add_executable(material_selftest tests/material_selftest.cpp src/client/systems/gfx/MaterialDb.cpp src/client/systems/entity/Materials.cpp src/util/JsonValue.cpp)
    target_include_directories(material_selftest PRIVATE src)
    target_compile_features(material_selftest PRIVATE cxx_std_23)
    add_test(NAME material_selftest COMMAND material_selftest)
    add_executable(replay_library_test
        tests/replay_library_test.cpp
        src/client/screen/screens/ReplayLibrary.cpp
        src/client/systems/replay/ReplayFormat.cpp
        src/client/systems/replay/ReplayController.cpp
        src/util/JsonValue.cpp
        src/util/Paths.cpp)
    target_include_directories(replay_library_test PRIVATE src "${glm_SOURCE_DIR}" "${concurrentqueue_SOURCE_DIR}")
    target_link_libraries(replay_library_test PRIVATE miniz Threads::Threads)
    target_compile_features(replay_library_test PRIVATE cxx_std_23)
    add_test(NAME replay_library_test COMMAND replay_library_test)
    add_executable(replay_selftest
        tests/replay_selftest.cpp
        src/client/systems/replay/ReplayFormat.cpp
        src/client/systems/replay/ReplayController.cpp
        src/util/JsonValue.cpp
        src/util/Paths.cpp)
    target_include_directories(replay_selftest PRIVATE src)
    target_link_libraries(replay_selftest PRIVATE miniz Threads::Threads)
    target_compile_features(replay_selftest PRIVATE cxx_std_23)
    add_test(NAME replay_selftest COMMAND replay_selftest)

    add_executable(ffmpeg_selftest
        tests/ffmpeg_selftest.cpp
        tests/stb_write_impl.cpp
        src/client/systems/replay/AviWriter.cpp
        src/client/systems/replay/Ffmpeg.cpp
        src/client/systems/replay/LibavEncoder.cpp
        src/client/systems/replay/Process.cpp
        src/util/Paths.cpp)
    target_include_directories(ffmpeg_selftest PRIVATE src "${stb_SOURCE_DIR}")
    target_link_libraries(ffmpeg_selftest PRIVATE miniz Threads::Threads)
    target_compile_features(ffmpeg_selftest PRIVATE cxx_std_23)

    add_executable(timeline_selftest
        tests/timeline_selftest.cpp
        src/client/systems/replay/Timeline.cpp
        src/util/JsonValue.cpp)
    target_include_directories(timeline_selftest PRIVATE src)
    target_compile_features(timeline_selftest PRIVATE cxx_std_23)
    add_test(NAME timeline_selftest COMMAND timeline_selftest)

    add_executable(level_undo_selftest
        tests/level_undo_selftest.cpp
        src/world/level/Level.cpp)
    target_include_directories(level_undo_selftest PRIVATE src)
    target_compile_features(level_undo_selftest PRIVATE cxx_std_23)
    add_test(NAME level_undo_selftest COMMAND level_undo_selftest)

    add_executable(cells16_selftest tests/cells16_selftest.cpp src/world/level/Level.cpp)
    target_include_directories(cells16_selftest PRIVATE src)
    add_test(NAME cells16_selftest COMMAND cells16_selftest)
    add_executable(chunk_mesher_golden_selftest
        tests/chunk_mesher_golden_selftest.cpp
        src/client/systems/renderer/chunk/ChunkMesher.cpp
        src/client/systems/gfx/Tessellator.cpp
        src/client/systems/resources/ServerBlocks.cpp
        src/util/Json.cpp
        src/world/level/Level.cpp
        src/world/level/block/BlockPalette.cpp
        src/world/level/chunk/ChunkCodec.cpp
        src/world/level/lighting/ChunkLighting.cpp
        src/world/level/lighting/LightRegistry.cpp)
    target_include_directories(chunk_mesher_golden_selftest PRIVATE src "${glm_SOURCE_DIR}")
    target_compile_features(chunk_mesher_golden_selftest PRIVATE cxx_std_23)
    target_link_libraries(chunk_mesher_golden_selftest PRIVATE glaze::glaze perlin_protocol Threads::Threads)
    add_test(NAME chunk_mesher_golden_selftest COMMAND chunk_mesher_golden_selftest)
    add_executable(timeline_editor_selftest
        tests/timeline_editor_selftest.cpp
        src/client/systems/replay/TimelineEditor.cpp
        src/client/systems/replay/Timeline.cpp
        src/util/JsonValue.cpp)
    target_include_directories(timeline_editor_selftest PRIVATE src)
    target_compile_features(timeline_editor_selftest PRIVATE cxx_std_23)
    add_test(NAME timeline_editor_selftest COMMAND timeline_editor_selftest)
    add_executable(export_form_selftest
        tests/export_form_selftest.cpp
        tests/stb_write_impl.cpp
        src/client/systems/replay/ExportForm.cpp
        src/client/systems/replay/ExportConfig.cpp
        src/client/systems/replay/AviWriter.cpp
        src/client/systems/replay/Ffmpeg.cpp
        src/client/systems/replay/LibavEncoder.cpp
        src/client/systems/replay/Process.cpp
        src/client/systems/replay/ReplayFormat.cpp
        src/util/JsonValue.cpp
        src/util/Paths.cpp)
    target_include_directories(export_form_selftest PRIVATE src "${stb_SOURCE_DIR}")
    target_link_libraries(export_form_selftest PRIVATE miniz Threads::Threads)
    target_compile_features(export_form_selftest PRIVATE cxx_std_23)
    add_test(NAME export_form_selftest COMMAND export_form_selftest)
    add_executable(hud_feed_selftest
        tests/hud_feed_selftest.cpp
        src/client/ui/HudFeed.cpp)
    target_include_directories(hud_feed_selftest PRIVATE src "${glm_SOURCE_DIR}" "${concurrentqueue_SOURCE_DIR}")
    target_compile_features(hud_feed_selftest PRIVATE cxx_std_23)
    add_test(NAME hud_feed_selftest COMMAND hud_feed_selftest)
    add_executable(sampler_rename_selftest
        tests/sampler_rename_selftest.cpp
        src/client/systems/javashaders/SamplerRename.cpp
        src/client/systems/javashaders/GlslTokens.cpp)
    target_include_directories(sampler_rename_selftest PRIVATE src)
    target_compile_features(sampler_rename_selftest PRIVATE cxx_std_23)
    add_test(NAME sampler_rename_selftest COMMAND sampler_rename_selftest)
    add_executable(nethernet_signal_selftest
        tests/nethernet_signal_selftest.cpp
        src/network/NetherNetSignal.cpp)
    target_include_directories(nethernet_signal_selftest PRIVATE src)
    target_compile_features(nethernet_signal_selftest PRIVATE cxx_std_23)
    add_test(NAME nethernet_signal_selftest COMMAND nethernet_signal_selftest)
    add_executable(url_handler_selftest
        tests/url_handler_selftest.cpp
        src/util/UrlHandlerRegistration.cpp)
    target_include_directories(url_handler_selftest PRIVATE src)
    target_compile_features(url_handler_selftest PRIVATE cxx_std_23)
    add_test(NAME url_handler_selftest COMMAND url_handler_selftest)
    add_executable(screen_manager_selftest
        tests/screen_manager_selftest.cpp
        src/client/screen/ScreenManager.cpp)
    target_include_directories(screen_manager_selftest PRIVATE src)
    target_compile_features(screen_manager_selftest PRIVATE cxx_std_23)
    add_test(NAME screen_manager_selftest COMMAND screen_manager_selftest)

    add_executable(launch_overrides_selftest
        tests/launch_overrides_selftest.cpp
        src/client/application/LaunchOverrides.cpp
        src/client/systems/bindings/KeyMapping.cpp
        src/client/systems/bindings/Keys.cpp)
    target_include_directories(launch_overrides_selftest PRIVATE src)
    target_compile_features(launch_overrides_selftest PRIVATE cxx_std_23)
    add_test(NAME launch_overrides_selftest COMMAND launch_overrides_selftest)

    add_test(NAME layering COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_layers.py ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME singleton_ratchet COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_singletons.py ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME embedded_markup COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_markup.py)
    add_test(NAME rml_format COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/format_rml.py --check)
    add_test(NAME resource_layout COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_resources.py "${PERLIN_EMBED_SOURCE}")
    add_executable(resources_selftest tests/resources_selftest.cpp src/util/Resources.cpp src/util/Paths.cpp
        src/client/platform/EmbeddedProvider.cpp ${PERLIN_EMBED_SOURCE} ${PERLIN_EMBED_NAMES_SOURCE})
    target_include_directories(resources_selftest PRIVATE src)
    target_compile_features(resources_selftest PRIVATE cxx_std_23)
    add_test(NAME resources_selftest COMMAND resources_selftest "${CMAKE_CURRENT_SOURCE_DIR}/resources")
    if(TARGET Perlin AND NOT CMAKE_CROSSCOMPILING AND NOT APPLE)
        set(PERLIN_BEHAVIOR_SELFTESTS handler_dispatch title_controller pack_manager server_browser audio_director light_band_cache debug_overlay player_interaction hud_screen hud_screen_preview studio worldview worldview_preview connection_controllers connection_controllers_preview title_ports)
        foreach(selftest IN LISTS PERLIN_BEHAVIOR_SELFTESTS)
            add_test(NAME ${selftest}_selftest COMMAND Perlin --selftest ${selftest})
            set(selftest_env "PERLIN_DATA_DIR=${CMAKE_BINARY_DIR}/selftest_data")
            if(selftest MATCHES "_preview$")
                list(APPEND selftest_env "PERLIN_PREVIEW=1")
            endif()
            if(selftest STREQUAL "connection_controllers_preview")
                list(APPEND selftest_env "PERLIN_PREVIEW_DISCONNECT=b" "PERLIN_PREVIEW_LOADING=1")
            endif()
            set_tests_properties(${selftest}_selftest PROPERTIES PASS_REGULAR_EXPRESSION "${selftest}_test: ok"
                                 ENVIRONMENT "${selftest_env}")
        endforeach()
        add_test(NAME executable_boot_resources COMMAND Perlin --help)
        set_tests_properties(executable_boot_resources PROPERTIES FAIL_REGULAR_EXPRESSION "required resource missing"
            PASS_REGULAR_EXPRESSION "Perlin options")
        if(UNIX AND Python3_FOUND)
            add_test(NAME runtime_smoke COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/runtime_smoke.py $<TARGET_FILE:Perlin>)
            set_tests_properties(runtime_smoke PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 900 LABELS runtime
                RUN_SERIAL TRUE)
        endif()
    endif()

    add_executable(asset_data_test tests/asset_data_test.cpp
        src/client/systems/entity/CapeGeometry.cpp
        src/client/systems/text/FontProviderTables.cpp
        src/client/systems/attachable/Geometry.cpp
        src/util/JsonValue.cpp src/util/Resources.cpp src/util/Paths.cpp
        src/client/platform/EmbeddedProvider.cpp ${PERLIN_EMBED_SOURCE} ${PERLIN_EMBED_NAMES_SOURCE})
    target_include_directories(asset_data_test PRIVATE src "${glm_SOURCE_DIR}")
    target_compile_features(asset_data_test PRIVATE cxx_std_23)
    add_test(NAME asset_data_test COMMAND asset_data_test "${CMAKE_CURRENT_SOURCE_DIR}/resources"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/legacy")
    add_executable(session_policy_selftest
        tests/session_policy_selftest.cpp
        src/client/systems/multiplayer/SessionPolicy.cpp)
    target_include_directories(session_policy_selftest PRIVATE src)
    target_compile_features(session_policy_selftest PRIVATE cxx_std_23)
    add_test(NAME session_policy_selftest COMMAND session_policy_selftest)
    add_executable(frame_tracker_selftest
        tests/frame_tracker_selftest.cpp
        src/client/systems/javashaders/FrameTracker.cpp
        src/client/systems/javashaders/Biomes.cpp)
    target_include_directories(frame_tracker_selftest PRIVATE src "${glm_SOURCE_DIR}")
    target_compile_features(frame_tracker_selftest PRIVATE cxx_std_23)
    add_test(NAME frame_tracker_selftest COMMAND frame_tracker_selftest)
    add_executable(pack_sky_mesh_selftest
        tests/pack_sky_mesh_selftest.cpp
        src/client/systems/renderer/gpu/PackSkyMesh.cpp)
    target_include_directories(pack_sky_mesh_selftest PRIVATE src "${glm_SOURCE_DIR}")
    target_compile_features(pack_sky_mesh_selftest PRIVATE cxx_std_23)
    add_test(NAME pack_sky_mesh_selftest COMMAND pack_sky_mesh_selftest)
    add_executable(shader_usage_selftest
        tests/shader_usage_selftest.cpp
        src/client/systems/javashaders/ShaderUsage.cpp)
    target_include_directories(shader_usage_selftest PRIVATE src)
    target_compile_features(shader_usage_selftest PRIVATE cxx_std_23)
    add_test(NAME shader_usage_selftest COMMAND shader_usage_selftest)
    add_executable(pack_convert_selftest
        tests/pack_convert_selftest.cpp
        src/client/systems/renderer/gpu/PackConvert.cpp)
    target_include_directories(pack_convert_selftest PRIVATE src "${glm_SOURCE_DIR}")
    target_compile_features(pack_convert_selftest PRIVATE cxx_std_23)
    add_test(NAME pack_convert_selftest COMMAND pack_convert_selftest)
    add_executable(pack_texture_util_selftest
        tests/pack_texture_util_selftest.cpp
        src/client/systems/javashaders/PackTextureUtil.cpp)
    target_include_directories(pack_texture_util_selftest PRIVATE src)
    target_compile_features(pack_texture_util_selftest PRIVATE cxx_std_23)
    add_test(NAME pack_texture_util_selftest COMMAND pack_texture_util_selftest)
    add_executable(shadow_math_selftest
        tests/shadow_math_selftest.cpp
        src/client/systems/renderer/gpu/ShadowMath.cpp)
    target_include_directories(shadow_math_selftest PRIVATE src "${glm_SOURCE_DIR}")
    target_compile_features(shadow_math_selftest PRIVATE cxx_std_23)
    add_test(NAME shadow_math_selftest COMMAND shadow_math_selftest)
    add_executable(post_effects_selftest
        tests/post_effects_selftest.cpp
        src/client/systems/renderer/gpu/PostEffects.cpp)
    target_include_directories(post_effects_selftest PRIVATE src)
    target_compile_features(post_effects_selftest PRIVATE cxx_std_23)
    add_test(NAME post_effects_selftest COMMAND post_effects_selftest)
    add_executable(chunk_selftest
        tests/chunk_selftest.cpp
        src/world/level/chunk/ChunkCodec.cpp)
    target_link_libraries(chunk_selftest PRIVATE perlin_protocol)
    add_test(NAME chunk_selftest COMMAND chunk_selftest)

    add_executable(packet_selftest tests/packet_selftest.cpp ${PERLIN_PROTOCOL_ROUNDTRIP})
    target_link_libraries(packet_selftest PRIVATE perlin_protocol)
    add_test(NAME packet_selftest COMMAND packet_selftest)

    add_executable(touch_input_selftest tests/touch_input_selftest.cpp
        src/client/systems/input/TouchControls.cpp src/client/systems/input/Input.cpp
        src/client/systems/bindings/Keys.cpp src/client/systems/bindings/KeyMapping.cpp)
    target_include_directories(touch_input_selftest PRIVATE src "${concurrentqueue_SOURCE_DIR}")
    target_compile_features(touch_input_selftest PRIVATE cxx_std_23)
    add_test(NAME touch_input_selftest COMMAND touch_input_selftest)
    add_executable(input_selftest
        tests/input_selftest.cpp
        src/client/systems/bindings/Keys.cpp
        src/client/systems/bindings/KeyMapping.cpp
        src/client/systems/renderer/Environment.cpp
        src/client/systems/renderer/Fog.cpp
        src/client/systems/gfx/Lightmap.cpp
        src/client/systems/gfx/SkyGeometry.cpp)
    target_include_directories(input_selftest PRIVATE src "${glm_SOURCE_DIR}")
    add_test(NAME input_selftest COMMAND input_selftest)

    add_executable(physics_selftest
        tests/physics_selftest.cpp
        src/world/movement/PlayerPhysics.cpp)
    target_include_directories(physics_selftest PRIVATE src)
    target_compile_features(physics_selftest PRIVATE cxx_std_23)
    add_test(NAME physics_selftest COMMAND physics_selftest)

    add_executable(physics_step_dump
        tests/physics_step_dump.cpp
        src/world/movement/PlayerPhysics.cpp)
    target_include_directories(physics_step_dump PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_compile_features(physics_step_dump PRIVATE cxx_std_23)
    set(PHYSICS_STEP_SEEDS "")
    foreach(seed RANGE 1 400)
        list(APPEND PHYSICS_STEP_SEEDS ${seed})
    endforeach()
    add_test(NAME physics_step_golden
        COMMAND ${CMAKE_COMMAND} -DPROGRAM=$<TARGET_FILE:physics_step_dump>
                "-DARGS=${PHYSICS_STEP_SEEDS}"
                -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/physics_step_golden.txt
                -P ${CMAKE_SOURCE_DIR}/tests/compare_output.cmake)

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
        src/world/movement/PlayerPhysics.cpp)
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

    add_executable(voxel_shapes_selftest tests/voxel_shapes_selftest.cpp src/world/movement/VoxelShapes.cpp)
    target_include_directories(voxel_shapes_selftest PRIVATE src)
    target_link_libraries(voxel_shapes_selftest PRIVATE perlin_protocol)
    add_test(NAME voxel_shapes_selftest COMMAND voxel_shapes_selftest)

    add_executable(bds_block_offset_selftest tests/bds_block_offset_selftest.cpp)
    target_include_directories(bds_block_offset_selftest PRIVATE src)
    target_compile_features(bds_block_offset_selftest PRIVATE cxx_std_23)
    add_test(NAME bds_block_offset_selftest COMMAND bds_block_offset_selftest
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/bds_block_offset.txt")

    add_executable(block_offset_definition_selftest tests/block_offset_definition_selftest.cpp
        src/client/systems/resources/BlockOffsetDefinition.cpp src/client/systems/resources/ServerBlocks.cpp
        src/util/JsonValue.cpp)
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
        src/world/movement/PlayerPhysics.cpp)
    target_include_directories(bedsim_trace_selftest PRIVATE src)
    target_compile_features(bedsim_trace_selftest PRIVATE cxx_std_23)
    add_test(NAME bedsim_trace_selftest COMMAND bedsim_trace_selftest "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/bedsim_traces.txt")

    add_executable(panorama_selftest tests/panorama_selftest.cpp src/client/ui/TitlePanorama.cpp)
    target_include_directories(panorama_selftest PRIVATE src)
    target_compile_features(panorama_selftest PRIVATE cxx_std_23)
    add_test(NAME panorama_selftest COMMAND panorama_selftest)

    add_executable(uitemplate_selftest tests/uitemplate_selftest.cpp src/client/ui/UiTemplate.cpp src/util/Resources.cpp src/util/Paths.cpp)
    target_include_directories(uitemplate_selftest PRIVATE src)
    target_compile_features(uitemplate_selftest PRIVATE cxx_std_23)
    add_test(NAME uitemplate_selftest COMMAND uitemplate_selftest)

    add_executable(trace_diff
        tests/trace_diff.cpp
        src/world/movement/PlayerPhysics.cpp)
    target_include_directories(trace_diff PRIVATE src)
    target_compile_features(trace_diff PRIVATE cxx_std_23)

    add_executable(damage_selftest tests/damage_selftest.cpp)
    target_include_directories(damage_selftest PRIVATE src "${glm_SOURCE_DIR}")
    target_compile_features(damage_selftest PRIVATE cxx_std_23)
    add_test(NAME damage_selftest COMMAND damage_selftest)

    add_executable(models_selftest tests/models_selftest.cpp src/client/systems/blocks/BlockModels.cpp)
    target_include_directories(models_selftest PRIVATE src)
    target_compile_features(models_selftest PRIVATE cxx_std_23)
    add_test(NAME models_selftest COMMAND models_selftest "${CMAKE_CURRENT_SOURCE_DIR}/resources/data/blocks/block_models.bin")

    file(GLOB TEXTURE_TEST_SOURCES CONFIGURE_DEPENDS
        src/client/systems/blocks/*.cpp src/client/systems/resources/TextureLoader.cpp src/client/systems/resources/TexturePack.cpp
        src/client/systems/resources/JavaPack.cpp)
    add_executable(texture_selftest
        tests/texture_selftest.cpp ${TEXTURE_TEST_SOURCES}
        src/client/systems/config/Options.cpp src/client/systems/bindings/KeyMapping.cpp
        src/util/Json.cpp src/util/Paths.cpp
        src/world/level/block/BlockPalette.cpp src/util/Resources.cpp
        src/client/systems/resources/ServerPackRegistry.cpp src/client/systems/resources/ServerBlocks.cpp src/util/JsonValue.cpp)
    target_include_directories(texture_selftest PRIVATE src "${stb_SOURCE_DIR}")
    target_link_libraries(texture_selftest PRIVATE glaze::glaze miniz Threads::Threads)
    target_compile_features(texture_selftest PRIVATE cxx_std_23)
    if(PERLIN_TEXTURE_TEST_ASSETS)
        add_test(NAME texture_selftest COMMAND texture_selftest "${PERLIN_TEXTURE_TEST_ASSETS}")
    endif()

    find_package(Python3 QUIET COMPONENTS Interpreter)
    add_executable(block_library_dump
        tests/block_library_dump.cpp ${TEXTURE_TEST_SOURCES}
        src/client/systems/config/Options.cpp src/client/systems/bindings/KeyMapping.cpp
        src/util/Json.cpp src/util/Paths.cpp
        src/world/level/block/BlockPalette.cpp src/util/Resources.cpp
        src/client/systems/resources/ServerPackRegistry.cpp src/client/systems/resources/ServerBlocks.cpp src/util/JsonValue.cpp)
    target_include_directories(block_library_dump PRIVATE src "${stb_SOURCE_DIR}")
    target_link_libraries(block_library_dump PRIVATE glaze::glaze miniz Threads::Threads)
    target_compile_features(block_library_dump PRIVATE cxx_std_23)
    if(Python3_FOUND)
        add_test(NAME block_library_golden
            COMMAND ${Python3_EXECUTABLE} -I "${CMAKE_SOURCE_DIR}/tests/block_library_golden.py"
                    $<TARGET_FILE:block_library_dump> "${CMAKE_SOURCE_DIR}/tests/block_library_golden.json")
    endif()

    add_executable(javapack_selftest
        tests/javapack_selftest.cpp src/client/systems/resources/JavaPack.cpp src/client/systems/resources/TexturePack.cpp
        src/client/systems/resources/TextureLoader.cpp src/util/Json.cpp)
    target_include_directories(javapack_selftest PRIVATE src "${stb_SOURCE_DIR}")
    target_link_libraries(javapack_selftest PRIVATE glaze::glaze miniz Threads::Threads)
    target_compile_features(javapack_selftest PRIVATE cxx_std_23)
    add_test(NAME javapack_selftest COMMAND javapack_selftest)

    add_executable(auth_selftest
        tests/auth_selftest.cpp
        src/network/XboxAuth.cpp src/network/XboxRelyingParties.cpp src/network/TokenStore.cpp src/network/AtomicFile.cpp src/network/SecretVault.cpp src/network/vault/DpapiVault.cpp src/network/vault/KeychainVault.cpp src/network/vault/LibsecretVault.cpp src/network/vault/KeystoreVault.cpp src/network/LoginBuilder.cpp src/network/ClientIdentity.cpp src/network/Http.cpp src/network/CaBundle.cpp src/network/PacketCipher.cpp
        src/client/systems/resources/SkinLoader.cpp src/client/systems/resources/TextureLoader.cpp src/util/Json.cpp)
    target_include_directories(auth_selftest PRIVATE src "${stb_SOURCE_DIR}" "${httplib_SOURCE_DIR}")
    target_compile_definitions(auth_selftest PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT OPENSSL_SUPPRESS_DEPRECATED)
    target_link_libraries(auth_selftest PRIVATE glaze::glaze miniz OpenSSL::SSL OpenSSL::Crypto Threads::Threads ${CMAKE_DL_LIBS})
    if(WIN32)
        target_link_libraries(auth_selftest PRIVATE ws2_32 crypt32 winhttp)
    elseif(APPLE)
        target_link_libraries(auth_selftest PRIVATE "-framework Security" "-framework CoreFoundation")
        target_compile_definitions(auth_selftest PRIVATE CPPHTTPLIB_USE_CERTS_FROM_MACOSX_KEYCHAIN)
    endif()
    target_compile_features(auth_selftest PRIVATE cxx_std_23)
    add_test(NAME auth_selftest COMMAND auth_selftest)

    add_executable(token_store_selftest
        tests/token_store_selftest.cpp src/network/TokenStore.cpp src/network/AtomicFile.cpp src/network/SecretVault.cpp src/network/vault/DpapiVault.cpp src/network/vault/KeychainVault.cpp src/network/vault/LibsecretVault.cpp src/network/vault/KeystoreVault.cpp src/util/Json.cpp)
    target_include_directories(token_store_selftest PRIVATE src)
    target_link_libraries(token_store_selftest PRIVATE glaze::glaze OpenSSL::Crypto Threads::Threads ${CMAKE_DL_LIBS})
    if(WIN32)
        target_link_libraries(token_store_selftest PRIVATE crypt32)
    elseif(APPLE)
        target_link_libraries(token_store_selftest PRIVATE "-framework Security" "-framework CoreFoundation")
    endif()
    target_compile_features(token_store_selftest PRIVATE cxx_std_23)
    add_test(NAME token_store_selftest COMMAND token_store_selftest)

    add_executable(rak_selftest tests/rak_selftest.cpp src/network/RakNet.cpp)
    add_executable(client_control_selftest tests/client_control_selftest.cpp
        src/client/services/control/ClientControl.cpp src/client/systems/input/Input.cpp src/client/systems/input/InputManager.cpp
        src/client/systems/bindings/KeyMapping.cpp src/client/systems/bindings/Keys.cpp src/client/systems/config/Options.cpp src/util/Json.cpp src/util/Resources.cpp src/util/Paths.cpp)
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

    add_executable(clientassets_selftest tests/clientassets_selftest.cpp src/client/systems/resources/ClientAssets.cpp src/client/systems/resources/TextureLoader.cpp src/util/Json.cpp)
    target_include_directories(clientassets_selftest PRIVATE src "${stb_SOURCE_DIR}")
    target_link_libraries(clientassets_selftest PRIVATE glaze::glaze miniz)
    target_compile_features(clientassets_selftest PRIVATE cxx_std_23)

    add_executable(shaderpack_selftest tests/shaderpack_selftest.cpp src/client/systems/resources/ShaderPack.cpp src/util/JsonValue.cpp tests/BundledShaderStub.cpp)
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

    if(PERLIN_WASM3)
        set(PERLIN_MOD_RUNTIME_SOURCE ${PERLIN_API_HOST_WASM3_SOURCES})
    else()
        set(PERLIN_MOD_RUNTIME_SOURCE ${PERLIN_API_HOST_WASMEDGE_SOURCES})
    endif()
    file(GLOB PERLIN_MOD_MANAGER_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/client/systems/mods/*.cpp")
    add_executable(mod_selftest tests/mod_selftest.cpp ${PERLIN_MOD_MANAGER_SOURCES} ${PERLIN_MOD_RUNTIME_SOURCE}
        ${PERLIN_API_HOST_CORE_SOURCES} src/util/JsonValue.cpp)
    target_include_directories(mod_selftest PRIVATE src ${PERLIN_API_INCLUDE_DIRS})
    target_link_libraries(mod_selftest PRIVATE ${PERLIN_WASM_ENGINE} Threads::Threads)
    target_compile_features(mod_selftest PRIVATE cxx_std_23)
    add_executable(mod_scope_test tests/mod_scope_test.cpp ${PERLIN_MOD_MANAGER_SOURCES} ${PERLIN_MOD_RUNTIME_SOURCE}
        ${PERLIN_API_HOST_CORE_SOURCES} src/util/JsonValue.cpp)
    target_include_directories(mod_scope_test PRIVATE src ${PERLIN_API_INCLUDE_DIRS})
    target_link_libraries(mod_scope_test PRIVATE ${PERLIN_WASM_ENGINE} Threads::Threads)
    target_compile_features(mod_scope_test PRIVATE cxx_std_23)
    add_test(NAME mod_scope_test COMMAND mod_scope_test)
    set(PERLIN_TEST_PLUGINS "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/plugins")
    if(PERLIN_WASM3)
        add_test(NAME mod_selftest COMMAND mod_selftest
            "${PERLIN_TEST_PLUGINS}/hello32.wasm" "${PERLIN_TEST_PLUGINS}/loop_32.wasm")
    else()
        add_test(NAME mod_selftest COMMAND mod_selftest
            "${PERLIN_TEST_PLUGINS}/hello64.wasm" "${PERLIN_TEST_PLUGINS}/hello32.wasm"
            "${PERLIN_TEST_PLUGINS}/loop_64.wasm" "${PERLIN_TEST_PLUGINS}/loop_32.wasm"
            "${CMAKE_CURRENT_SOURCE_DIR}/plugins/prebuilt/waila.wasm")
    endif()

    find_package(Python3 QUIET COMPONENTS Interpreter)
    if(Python3_FOUND)
        perlin_api_add_checks(CLIENT "${CMAKE_CURRENT_SOURCE_DIR}/src/client" SERVER "${CMAKE_CURRENT_SOURCE_DIR}/server/src")
        if(NOT MSVC)
            set(PERLIN_PLUGINS_GEN "${CMAKE_CURRENT_BINARY_DIR}/plugins_gen")
            add_custom_command(OUTPUT "${PERLIN_PLUGINS_GEN}/waila/mod_settings.hpp"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${PERLIN_PLUGINS_GEN}/waila"
                COMMAND ${Python3_EXECUTABLE} "${PERLIN_API_DIR}/tools/perlin_settings.py"
                        "${CMAKE_CURRENT_SOURCE_DIR}/plugins/waila/waila.json" "${PERLIN_PLUGINS_GEN}/waila/mod_settings.hpp"
                DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/plugins/waila/waila.json" "${PERLIN_API_DIR}/tools/perlin_settings.py")
            add_executable(waila_test plugins/waila/tests/waila_test.cpp "${PERLIN_PLUGINS_GEN}/waila/mod_settings.hpp")
            target_include_directories(waila_test PRIVATE plugins/waila "${PERLIN_PLUGINS_GEN}/waila")
            target_compile_definitions(waila_test PRIVATE MANIFEST="${CMAKE_CURRENT_SOURCE_DIR}/plugins/waila/waila.json")
            target_link_libraries(waila_test PRIVATE perlin_api)
            add_test(NAME waila_test COMMAND waila_test)
            if(APPLE)
                target_compile_definitions(waila_test PRIVATE _LIBCPP_DISABLE_AVAILABILITY)
            endif()
        endif()
    endif()

    get_property(perlin_all_tests DIRECTORY PROPERTY TESTS)
    foreach(perlin_test IN LISTS perlin_all_tests)
        set_property(TEST ${perlin_test} APPEND PROPERTY ENVIRONMENT_MODIFICATION "PERLIN_RESOURCES=set:${CMAKE_CURRENT_SOURCE_DIR}/resources")
    endforeach()
endif()

get_property(PERLIN_LOCAL_TARGETS DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(PERLIN_LOCAL_TARGET IN LISTS PERLIN_LOCAL_TARGETS)
    if(PERLIN_LOCAL_TARGET MATCHES "^(Perlin|trace_diff)$|selftest$")
        target_include_directories(${PERLIN_LOCAL_TARGET} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
    endif()
endforeach()

option(PERLIN_BUILD_SERVER "Build the Perlin server: singleplayer in the client and the dedicated executable" ON)
if(PERLIN_BUILD_SERVER)
    set(PERLIN_SERVER_EMBED ON CACHE BOOL "" FORCE)

    set(PERLIN_SERVER_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    if(EMSCRIPTEN OR ANDROID OR PERLIN_IOS)
        set(PERLIN_SERVER_BUILD_EXE OFF CACHE BOOL "" FORCE)
        set(PERLIN_SERVER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    endif()
    if(EMSCRIPTEN)

        set(PERLIN_SERVER_NETHERNET OFF CACHE BOOL "" FORCE)
    endif()
    add_subdirectory(server)
    if(TARGET Perlin AND TARGET perlin_server_embed)
        target_sources(Perlin PRIVATE server/client_bridge/EmbeddedServer.cpp)
        target_include_directories(Perlin PRIVATE server/client_bridge server/include)
        target_compile_definitions(Perlin PRIVATE PERLIN_SINGLEPLAYER=1 PERLIN_SERVER_STATIC)
        target_link_libraries(Perlin PRIVATE perlin_server_embed)
    endif()

    if(TARGET perlin-server)
        install(TARGETS perlin-server RUNTIME DESTINATION .)
        install(FILES resources/data/blocks/block_states.json resources/data/blocks/block_destroy.tsv DESTINATION resources/data/blocks)
        install(FILES resources/data/items/extra_items.txt DESTINATION resources/data/items)
    endif()
endif()
