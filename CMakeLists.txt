cmake_minimum_required(VERSION 3.31)

if(APPLE)
    project(GutterPress VERSION 0.1.0 LANGUAGES C CXX OBJC OBJCXX)
else()
    project(GutterPress VERSION 0.1.0 LANGUAGES C CXX)
endif()

option(GUTTERPRESS_BUILD_PLUGIN "Build the YUP Standalone/VST3/AU targets" ON)
option(GUTTERPRESS_BUILD_TESTS "Build deterministic DSP regression tests" ON)
option(GUTTERPRESS_ADHOC_SIGN "Ad-hoc sign local macOS plugin and app bundles after building" ON)
set(GUTTERPRESS_CREATE_AU OFF)
if(APPLE)
    set(GUTTERPRESS_CREATE_AU ON)
endif()
set(GUTTERPRESS_YUP_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../yup" CACHE PATH
    "Path to a local YUP checkout; fetched from upstream when unavailable")

add_library(gutterpress_engine STATIC
    source/GutterPressEngine.cpp)
target_include_directories(gutterpress_engine PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/include")
target_compile_features(gutterpress_engine PUBLIC cxx_std_20)
set_target_properties(gutterpress_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(gutterpress_engine PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(GUTTERPRESS_BUILD_TESTS)
    include(CTest)

    function(enable_test_assertions target_name)
        if(MSVC)
            target_compile_options(${target_name} PRIVATE /UNDEBUG)
        elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
            target_compile_options(${target_name} PRIVATE -UNDEBUG)
        endif()
    endfunction()

    add_executable(gutterpress_engine_tests
        tests/GutterPressEngineTests.cpp)
    target_link_libraries(gutterpress_engine_tests PRIVATE gutterpress_engine)
    target_compile_features(gutterpress_engine_tests PRIVATE cxx_std_20)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(gutterpress_engine_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    enable_test_assertions(gutterpress_engine_tests)
    add_test(NAME gutterpress_engine_tests COMMAND gutterpress_engine_tests)
endif()

if(GUTTERPRESS_BUILD_PLUGIN)
    set(YUP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(YUP_BUILD_PLUGINS OFF CACHE BOOL "" FORCE)
    set(YUP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(YUP_ENABLE_PLUGINVAL OFF CACHE BOOL "" FORCE)
    set(YUP_ENABLE_VST3_VALIDATOR OFF CACHE BOOL "" FORCE)
    set(YUP_ENABLE_CLAP_VALIDATOR OFF CACHE BOOL "" FORCE)
    set(YUP_ENABLE_AUVAL_VALIDATOR OFF CACHE BOOL "" FORCE)

    if(EXISTS "${GUTTERPRESS_YUP_SOURCE_DIR}/CMakeLists.txt")
        add_subdirectory("${GUTTERPRESS_YUP_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/yup")
    else()
        include(FetchContent)
        FetchContent_Declare(yup
            GIT_REPOSITORY https://github.com/kunitoki/yup.git
            GIT_TAG 9a1c9bc699b6a714f6f52486462d98a140c8bf95)
        FetchContent_MakeAvailable(yup)
    endif()

    # YUP's iconutil-based helper rejects its generated iconset on current
    # macOS. Keep the compatibility fix project-local until upstream changes.
    if(APPLE)
        include(cmake/YupMacOSIconWorkaround.cmake)
    endif()

    yup_audio_plugin(
        TARGET_NAME gutterpress
        TARGET_VERSION ${PROJECT_VERSION}
        TARGET_IDE_GROUP "GutterPress"
        TARGET_APP_ID "audio.2bit.gutterpress"
        TARGET_APP_NAMESPACE "audio.2bit"
        TARGET_CXX_STANDARD 20
        PLUGIN_ID "audio.2bit.GutterPress"
        PLUGIN_NAME "GutterPress"
        PLUGIN_VENDOR "2bit"
        PLUGIN_EMAIL ""
        PLUGIN_VERSION "${PROJECT_VERSION}"
        PLUGIN_DESCRIPTION "Broken stereo bit-crush, gate, and tone effect"
        PLUGIN_URL "https://github.com/2bbb/GutterPress"
        PLUGIN_IS_SYNTH OFF
        PLUGIN_IS_MONO OFF
        PLUGIN_CREATE_CLAP OFF
        PLUGIN_CREATE_VST3 ON
        PLUGIN_CREATE_AU ${GUTTERPRESS_CREATE_AU}
        PLUGIN_AU_SUBTYPE "GtPr"
        PLUGIN_AU_MANUFACTURER "2Bit"
        PLUGIN_AU_SANDBOX_SAFE ON
        PLUGIN_CREATE_AUv3 OFF
        PLUGIN_CREATE_LV2 OFF
        PLUGIN_CREATE_AAX OFF
        PLUGIN_CREATE_STANDALONE ON
        PLUGIN_COPY_AFTER_BUILD OFF
        MODULES
            yup::yup_gui
            yup::yup_audio_processors)

    target_sources(gutterpress_shared INTERFACE
        source/ParameterGridEditor.cpp
        source/ParameterGridEditor.h
        source/ProductState.h
        source/GutterPressPlugin.cpp
        source/GutterPressPlugin.h)
    target_link_libraries(gutterpress_shared INTERFACE gutterpress_engine)
    target_include_directories(gutterpress_shared INTERFACE
        "${CMAKE_CURRENT_LIST_DIR}/include"
        "${CMAKE_CURRENT_LIST_DIR}/source")

    if(GUTTERPRESS_BUILD_TESTS)
        add_executable(gutterpress_plugin_bridge_tests
            tests/GutterPressPluginBridgeTests.cpp
            source/GutterPressPlugin.cpp)
        target_link_libraries(gutterpress_plugin_bridge_tests PRIVATE gutterpress_engine yup::yup_audio_processors)
        target_include_directories(gutterpress_plugin_bridge_tests PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/include"
            "${CMAKE_CURRENT_LIST_DIR}/source")
        target_compile_definitions(gutterpress_plugin_bridge_tests PRIVATE GUTTERPRESS_HEADLESS_TEST=1)
        target_compile_features(gutterpress_plugin_bridge_tests PRIVATE cxx_std_20)
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
            target_compile_options(gutterpress_plugin_bridge_tests PRIVATE -Wall -Wextra -Wpedantic)
        endif()
        enable_test_assertions(gutterpress_plugin_bridge_tests)
        add_test(NAME gutterpress_plugin_bridge_tests COMMAND gutterpress_plugin_bridge_tests)

        add_executable(gutterpress_standalone_bridge_tests
            tests/GutterPressStandaloneBridgeTests.cpp
            source/GutterPressPlugin.cpp)
        target_link_libraries(gutterpress_standalone_bridge_tests PRIVATE gutterpress_engine yup::yup_audio_processors)
        target_include_directories(gutterpress_standalone_bridge_tests PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/include"
            "${CMAKE_CURRENT_LIST_DIR}/source")
        target_compile_definitions(gutterpress_standalone_bridge_tests PRIVATE
            GUTTERPRESS_HEADLESS_TEST=1
            YUP_AUDIO_PLUGIN_ENABLE_STANDALONE=1)
        target_compile_features(gutterpress_standalone_bridge_tests PRIVATE cxx_std_20)
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
            target_compile_options(gutterpress_standalone_bridge_tests PRIVATE -Wall -Wextra -Wpedantic)
        endif()
        enable_test_assertions(gutterpress_standalone_bridge_tests)
        add_test(NAME gutterpress_standalone_bridge_tests COMMAND gutterpress_standalone_bridge_tests)
    endif()

    set(gutterpress_release_bundle_targets
        gutterpress_standalone_plugin
        gutterpress_vst3_plugin)
    if(APPLE)
        list(APPEND gutterpress_release_bundle_targets gutterpress_au_plugin)
    endif()

    add_custom_target(gutterpress_release_bundles
        DEPENDS ${gutterpress_release_bundle_targets})

    if(APPLE)
        set_target_properties(gutterpress_standalone_plugin PROPERTIES
            MACOSX_BUNDLE_GUI_IDENTIFIER "audio.2bit.gutterpress"
            MACOSX_BUNDLE_BUNDLE_NAME "GutterPress"
            XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "audio.2bit.gutterpress")
    endif()

    if(APPLE AND GUTTERPRESS_ADHOC_SIGN)
        find_program(GUTTERPRESS_CODESIGN_EXECUTABLE codesign REQUIRED)
        add_custom_command(TARGET gutterpress_vst3_plugin POST_BUILD
            COMMAND "${GUTTERPRESS_CODESIGN_EXECUTABLE}" --force --deep --sign -
                    "$<TARGET_BUNDLE_DIR:gutterpress_vst3_plugin>"
            COMMENT "Ad-hoc signing GutterPress VST3 for local loading"
            VERBATIM)
        add_custom_command(TARGET gutterpress_standalone_plugin POST_BUILD
            COMMAND "${GUTTERPRESS_CODESIGN_EXECUTABLE}" --force --deep --sign -
                    "$<TARGET_BUNDLE_DIR:gutterpress_standalone_plugin>"
            COMMENT "Ad-hoc signing GutterPress standalone app for local launch"
            VERBATIM)
    endif()
endif()
