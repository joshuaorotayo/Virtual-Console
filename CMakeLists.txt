cmake_minimum_required(VERSION 3.22)

project(VirtualConsole VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

if(MINGW)
    # GCC 16 does not put C string functions in the global namespace. JUCE calls them unqualified.
    add_compile_options(-include string.h)
endif()

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.15
    GIT_SHALLOW TRUE
    PATCH_COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch-juce-mingw.cmake
)
FetchContent_MakeAvailable(JUCE)

set(VC_CORE_SOURCES
    src/model/ConsoleModel.cpp
    src/engine/SharedSource.cpp
    src/engine/MixEngine.cpp
    src/engine/ChannelDsp.cpp
)

function(vc_configure_target target)
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(${target} PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_ASIO=0
        JUCE_USE_MP3AUDIOFORMAT=1
        JUCE_USE_WINDOWS_MEDIA_FORMAT=0
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:${target},JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}"
    )
    if(MINGW)
        target_compile_options(${target} PRIVATE -Wa,-mbig-obj)
    endif()
endfunction()

juce_add_gui_app(VirtualConsole
    PRODUCT_NAME "Virtual Console"
    COMPANY_NAME "VirtualConsole"
    BUNDLE_ID com.virtualconsole.app
)

target_sources(VirtualConsole PRIVATE
    src/Main.cpp
    src/ui/MainComponent.cpp
    src/ui/ChannelStrip.cpp
    src/ui/ChannelEditor.cpp
    ${VC_CORE_SOURCES}
)

vc_configure_target(VirtualConsole)

set(VC_MINGW_LIBS "")
if(MINGW)
    set(VC_MINGW_LIBS
        shcore comctl32 imm32 dwmapi dcomp d3d11 dxgi d2d1 dwrite
        windowscodecs ole32 oleaut32 uuid shell32 shlwapi version
        ws2_32 winmm wininet advapi32 user32 gdi32 dbghelp sapi ksguid)
    set(VC_MINGW_START "${CMAKE_CURRENT_BINARY_DIR}/mingw-start")
    file(MAKE_DIRECTORY "${VC_MINGW_START}")
    find_program(VC_WINDRES windres REQUIRED)
    execute_process(
        COMMAND "${VC_WINDRES}" -O coff "${CMAKE_CURRENT_SOURCE_DIR}/cmake/windows-manifest.rc" "${VC_MINGW_START}/default-manifest.o"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/cmake"
        COMMAND_ERROR_IS_FATAL ANY)
    # A GUI subsystem link rejects a second manifest. Replace MinGW's default one.
    target_link_options(VirtualConsole PRIVATE "-B${VC_MINGW_START}")
endif()

target_link_libraries(VirtualConsole PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_formats
    juce::juce_audio_devices
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
    ${VC_MINGW_LIBS}
)

juce_add_console_app(VirtualConsoleTests
    PRODUCT_NAME "Virtual Console Tests"
)

target_sources(VirtualConsoleTests PRIVATE
    tests/TestMain.cpp
    tests/MixTests.cpp
    ${VC_CORE_SOURCES}
)

if(MINGW)
    target_link_options(VirtualConsoleTests PRIVATE "-B${VC_MINGW_START}")
endif()

vc_configure_target(VirtualConsoleTests)

target_link_libraries(VirtualConsoleTests PRIVATE
    juce::juce_audio_formats
    juce::juce_audio_basics
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
    ${VC_MINGW_LIBS}
)

enable_testing()
add_test(NAME mix COMMAND VirtualConsoleTests)
