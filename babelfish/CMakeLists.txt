cmake_minimum_required(VERSION 3.21)
project(FulDCBabelfish LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Link the C++ runtime statically. A plugin is dropped into somebody else's
# client, and a missing VC++ redistributable makes it fail to load with no
# useful explanation. Costs about 100 kB and removes the dependency entirely.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Everything that does not touch the plugin API, so the same objects build into
# both the DLL and the standalone test harness.
set(TRANSLATE_CORE_SOURCES
    src/AzureTranslator.cpp
    src/Cache.cpp
    src/ClaudeTranslator.cpp
    src/DeepLTranslator.cpp
    src/GoogleTranslator.cpp
    src/Http.cpp
    src/Json.cpp
    src/Languages.cpp
    src/Incoming.cpp
    src/MessageFilter.cpp
    src/MyMemoryTranslator.cpp
    src/Translators.cpp
)

add_library(Babelfish SHARED
    src/dllmain.cpp
    src/ChatInput.cpp
    src/Hubs.cpp
    src/Nicks.cpp
    src/Marshal.cpp
    src/Settings.cpp
    src/SettingsDialog.cpp
    src/TranslateQueue.cpp
    ${TRANSLATE_CORE_SOURCES}
    res/Babelfish.rc
)

target_include_directories(Babelfish PRIVATE src)

target_compile_definitions(Babelfish PRIVATE
    UNICODE _UNICODE
    WIN32_LEAN_AND_MEAN NOMINMAX
)

target_link_libraries(Babelfish PRIVATE user32 comctl32 winhttp advapi32)

# The host looks up "pluginInit" by name; keep the output name stable.
set_target_properties(Babelfish PROPERTIES
    OUTPUT_NAME "Babelfish-x64"
    PREFIX ""
)

if(MSVC)
    target_compile_options(Babelfish PRIVATE /W4 /permissive- /utf-8)

    # A .map file turns an address in a crash dump into a function name with
    # nothing but a text lookup. There is no debugger on the build machine and
    # the host ships no symbols for plugins, so this is the only way a minidump
    # from a user's client can be read. It is linker output only and does not
    # affect code generation, so the shipped DLL is unchanged.
    target_link_options(Babelfish PRIVATE /MAP)
endif()

# Standalone harness for the filtering, splitting, JSON and cache logic, so
# those can be reproduced without loading the plugin into a DC client.
add_executable(translatetest
    test/main.cpp
    ${TRANSLATE_CORE_SOURCES}
)
target_include_directories(translatetest PRIVATE src)
target_compile_definitions(translatetest PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(translatetest PRIVATE user32 winhttp)
if(MSVC)
    target_compile_options(translatetest PRIVATE /W4 /permissive- /utf-8)
endif()
