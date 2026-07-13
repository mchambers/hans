cmake_minimum_required(VERSION 3.10)
project(Hans C)

# Hans — a Mac OS 9 markdown writing environment.
# Build with the Retro68 PowerPC toolchain:
#   cmake -B build -DCMAKE_TOOLCHAIN_FILE=$HOME/Retro68-build/toolchain/powerpc-apple-macos/cmake/retroppc.toolchain.cmake
#   cmake --build build

add_application(Hans
    TYPE "APPL"
    CREATOR "HANS"
    src/main.c
    src/prefs.c
    src/dialogutil.c
    src/markdown.c
    src/editor.c
    src/library.c
    src/preview.c
    src/stylecheck.c
    rsrc/hans.r
)

target_include_directories(Hans PRIVATE src)

target_link_libraries(Hans
    -lTextension          # MLTE styled text engine
    -lNavigationLib       # Navigation Services
    -lAppearanceLib
    -lControlsLib
    -lWindowsLib
    -lMenusLib
    -lDialogsLib
    -lTextCommon
    -lIconServicesLib
)

set_target_properties(Hans PROPERTIES COMPILE_OPTIONS "-ffunction-sections")
set_target_properties(Hans PROPERTIES LINK_FLAGS "-Wl,-gc-sections")
