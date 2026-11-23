# Applied from the JUCE source directory by FetchContent PATCH_COMMAND.
# JUCE 8 stops the build on MinGW. The Win32 sources still compile once the
# 64-bit macros are defined, which is enough for this app's Windows toolchain.
if(NOT EXISTS "modules/juce_core/system/juce_TargetPlatform.h")
    return()
endif()

file(READ "modules/juce_core/system/juce_TargetPlatform.h" text)
string(FIND "${text}" "MinGW is not supported" found)
if(NOT found EQUAL -1)
    string(REPLACE
        "  #ifdef __MINGW32__\n    #error \"MinGW is not supported. Please use an alternative compiler.\"\n  #endif"
        "  #if defined(__MINGW32__) || defined(__MINGW64__)\n    #if defined(__x86_64__) || defined(_WIN64)\n      #define JUCE_64BIT 1\n    #else\n      #define JUCE_32BIT 1\n    #endif\n  #endif"
        text "${text}")
    file(WRITE "modules/juce_core/system/juce_TargetPlatform.h" "${text}")
endif()

set(standard "modules/juce_core/system/juce_StandardHeader.h")
if(EXISTS "${standard}")
    file(READ "${standard}" header)
    string(FIND "${header}" "JUCE_WINDOWS && ! JUCE_MSVC" already)
    if(already EQUAL -1)
        string(REPLACE
            "#if JUCE_LINUX || JUCE_BSD"
            "#if JUCE_WINDOWS && ! JUCE_MSVC\n #include <string.h>\n #include <cstring>\n #ifndef stricmp\n  #define stricmp _stricmp\n #endif\n #ifndef strnicmp\n  #define strnicmp _strnicmp\n #endif\n#endif\n\n#if JUCE_LINUX || JUCE_BSD"
            header "${header}")
        file(WRITE "${standard}" "${header}")
    endif()
endif()

set(gui "modules/juce_gui_basics/juce_gui_basics.cpp")
if(EXISTS "${gui}")
    file(READ "${gui}" gui_text)
    string(FIND "${gui_text}" "CaretPosition_Unknown = 0" caret_done)
    if(caret_done EQUAL -1)
        string(REPLACE
            "#include <uiautomation.h>"
            "#include <uiautomation.h>\n\n #if defined(__MINGW32__) || defined(__MINGW64__)\n  #ifndef CaretPosition_Unknown\n   enum CaretPosition\n   {\n       CaretPosition_Unknown = 0,\n       CaretPosition_EndOfLine = 1,\n       CaretPosition_BeginningOfLine = 2\n   };\n  #endif\n #endif"
            gui_text "${gui_text}")
        file(WRITE "${gui}" "${gui_text}")
    endif()
endif()

set(juceaide_lists "extras/Build/juceaide/CMakeLists.txt")
if(EXISTS "${juceaide_lists}")
    file(READ "${juceaide_lists}" juceaide_text)
    string(FIND "${juceaide_text}" "windows-manifest.rc" manifest_done)
    if(manifest_done EQUAL -1)
        get_filename_component(vc_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
        file(TO_CMAKE_PATH "${vc_root}/cmake/windows-manifest.rc" manifest_rc)
        string(REPLACE
            "    if(MINGW)\n        target_link_libraries(juceaide PRIVATE"
            "    if(MINGW)\n        target_sources(juceaide PRIVATE \"${manifest_rc}\")\n        target_link_libraries(juceaide PRIVATE"
            juceaide_text "${juceaide_text}")
        file(WRITE "${juceaide_lists}" "${juceaide_text}")
    endif()
endif()
