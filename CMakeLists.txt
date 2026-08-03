cmake_minimum_required(VERSION 3.21)
project(Squiggle LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Link the C++ runtime statically. A plugin is dropped into somebody else's
# client, and a missing VC++ redistributable makes it fail to load with no
# useful explanation. Costs about 100 kB and removes the dependency entirely.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(Squiggle SHARED
    src/Plugin.cpp
    src/AutoCorrect.cpp
    src/ChatInput.cpp
    src/Nicks.cpp
    src/Settings.cpp
    src/SettingsDialog.cpp
    src/Speller.cpp
    src/Strings.cpp
    src/TextFile.cpp
    src/Tokenizer.cpp
    res/Squiggle.rc
)

target_include_directories(Squiggle PRIVATE src)

target_compile_definitions(Squiggle PRIVATE
    UNICODE _UNICODE
    WIN32_LEAN_AND_MEAN NOMINMAX
)

target_link_libraries(Squiggle PRIVATE user32 gdi32 comctl32 comdlg32 shell32 ole32 oleaut32)

# The host looks up "pluginInit" by name; __stdcall on x64 needs no decoration fix,
# but keep the output name stable and predictable.
set_target_properties(Squiggle PROPERTIES
    OUTPUT_NAME "Squiggle-x64"
    PREFIX ""
)

if(MSVC)
    target_compile_options(Squiggle PRIVATE /W4 /permissive- /utf-8)
endif()

# Standalone harness for the checking logic, so tokenizer and dictionary bugs
# can be reproduced without loading the plugin into a DC client.
add_executable(squiggletest
    test/main.cpp
    src/AutoCorrect.cpp
    src/Nicks.cpp
    src/Settings.cpp
    src/SettingsDialog.cpp
    src/Speller.cpp
    src/Strings.cpp
    src/TextFile.cpp
    src/Tokenizer.cpp
    res/Squiggle.rc
)
target_include_directories(squiggletest PRIVATE src)
target_compile_definitions(squiggletest PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(squiggletest PRIVATE user32 gdi32 comctl32 comdlg32 shell32 ole32 oleaut32)
if(MSVC)
    target_compile_options(squiggletest PRIVATE /W4 /permissive- /utf-8)
endif()
