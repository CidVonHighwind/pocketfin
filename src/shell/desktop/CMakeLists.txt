# The same files the console builds, over the desktop port. A green suite here
# is not a working screen on the console.

cmake_minimum_required(VERSION 3.16)

if(POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "Embedded")
endif()

project(pocketfin_desktop C)
set(CMAKE_C_STANDARD 99)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

# stb_image decodes far more than the console's Media Engine accepts;
# desktop/jpeg.c enforces the console's rules in front of it.
include(FetchContent)
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        f0569113c93ad095470c54bf34a17b36646bbbb5
    GIT_SHALLOW    FALSE
)
FetchContent_MakeAvailable(stb)

# Never globbed: globbing is what let files drift in.
set(POCKETFIN_MANIFEST "${REPO_ROOT}/src/sources.txt")

# Or a newly named file is never built until somebody deletes the cache.
set_property(DIRECTORY APPEND PROPERTY
             CMAKE_CONFIGURE_DEPENDS "${POCKETFIN_MANIFEST}")

function(pocketfin_section name out)
    file(STRINGS "${POCKETFIN_MANIFEST}" lines)
    set(result "")
    set(inside FALSE)
    foreach(line IN LISTS lines)
        string(STRIP "${line}" line)
        if(line MATCHES "^\\[(.+)\\]$")
            if("${CMAKE_MATCH_1}" STREQUAL "${name}")
                set(inside TRUE)
            else()
                set(inside FALSE)
            endif()
        elseif(inside AND NOT line STREQUAL "" AND NOT line MATCHES "^#")
            list(APPEND result "${REPO_ROOT}/${line}")
        endif()
    endforeach()
    if(result STREQUAL "")
        message(FATAL_ERROR
                "src/sources.txt has no [${name}] section, or it is empty")
    endif()
    set(${out} "${result}" PARENT_SCOPE)
endfunction()

# At configure time, so a wrong-way include fails before anything compiles.
find_program(POCKETFIN_SH NAMES sh bash)
if(POCKETFIN_SH)
    execute_process(COMMAND ${POCKETFIN_SH} ${REPO_ROOT}/tools/layers.sh
                    WORKING_DIRECTORY ${REPO_ROOT}
                    OUTPUT_VARIABLE layers_out
                    ERROR_VARIABLE  layers_err
                    RESULT_VARIABLE layers_rc)
    if(NOT layers_rc EQUAL 0)
        message(FATAL_ERROR "${layers_out}${layers_err}")
    endif()
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)

execute_process(
    COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/tools/font_bake.py
            ${REPO_ROOT}/assets/font.bin ${REPO_ROOT}/build/gen
    RESULT_VARIABLE font_rc)
if(NOT font_rc EQUAL 0)
    message(FATAL_ERROR "font_bake.py failed (${font_rc})")
endif()

execute_process(
    COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/tools/register_gen.py
            ${POCKETFIN_MANIFEST} ${REPO_ROOT}/build/gen
    RESULT_VARIABLE reg_rc)
if(NOT reg_rc EQUAL 0)
    message(FATAL_ERROR "register_gen.py failed (${reg_rc})")
endif()

pocketfin_section(generated    POCKETFIN_GENERATED)
pocketfin_section(base         POCKETFIN_BASE)
pocketfin_section(io           POCKETFIN_IO)
pocketfin_section(jelly        POCKETFIN_JELLY)
pocketfin_section(model        POCKETFIN_MODEL)
pocketfin_section(view         POCKETFIN_VIEW)
pocketfin_section(app          POCKETFIN_APP)
pocketfin_section(page         POCKETFIN_PAGE)
pocketfin_section(tools        POCKETFIN_TOOLS)
pocketfin_section(tests        POCKETFIN_TESTS)
pocketfin_section(desktop      POCKETFIN_DESKTOP)
pocketfin_section(desktop-app  POCKETFIN_DESKTOP_APP)
pocketfin_section(desktop-test POCKETFIN_DESKTOP_TEST)


# ffmpeg, found from PATH: its headers and import libraries sit beside the exe.
# Override with -DPOCKETFIN_FFMPEG=<dir>. Without it the build still works and
# this machine cannot decode a film.
set(POCKETFIN_FFMPEG "" CACHE PATH "Root of an ffmpeg build: include/ lib/ bin/")

if(NOT POCKETFIN_FFMPEG)
    find_program(POCKETFIN_FFMPEG_EXE ffmpeg)
    if(POCKETFIN_FFMPEG_EXE)
        get_filename_component(_ff_bin "${POCKETFIN_FFMPEG_EXE}" DIRECTORY)
        get_filename_component(POCKETFIN_FFMPEG "${_ff_bin}" DIRECTORY)
    endif()
endif()

set(POCKETFIN_HAVE_FFMPEG OFF)
if(POCKETFIN_FFMPEG AND EXISTS "${POCKETFIN_FFMPEG}/include/libavcodec/avcodec.h")
    set(POCKETFIN_HAVE_FFMPEG ON)
    message(STATUS "ffmpeg: ${POCKETFIN_FFMPEG}")
else()
    message(STATUS "ffmpeg: not found -- this build cannot decode a film")
endif()

set(POCKETFIN_SOURCES
    ${POCKETFIN_GENERATED}
    ${POCKETFIN_BASE}
    ${POCKETFIN_IO}
    ${POCKETFIN_JELLY}
    ${POCKETFIN_MODEL}
    ${POCKETFIN_VIEW}
    ${POCKETFIN_PAGE}
    ${POCKETFIN_APP}
    ${POCKETFIN_TOOLS}
    ${POCKETFIN_DESKTOP}
)

set(POCKETFIN_INCLUDES
    ${REPO_ROOT}/src
    ${REPO_ROOT}/tests
)

add_executable(pocketfin-test
    ${POCKETFIN_SOURCES}
    ${REPO_ROOT}/build/gen/register.c
    ${POCKETFIN_TESTS}
    ${POCKETFIN_DESKTOP_TEST}
)

target_include_directories(pocketfin-test PRIVATE ${POCKETFIN_INCLUDES}
    ${stb_SOURCE_DIR})

function(pocketfin_ffmpeg target)
    if(POCKETFIN_HAVE_FFMPEG)
        target_include_directories(${target} PRIVATE "${POCKETFIN_FFMPEG}/include")
        target_link_directories(${target} PRIVATE "${POCKETFIN_FFMPEG}/lib")
        target_link_libraries(${target} PRIVATE avformat avcodec avutil swscale)
        target_compile_definitions(${target} PRIVATE POCKETFIN_FFMPEG=1)
        # A binary that cannot find the DLLs fails at start-up with a dialog
        # rather than a message anybody can act on.
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${POCKETFIN_FFMPEG}/bin/avformat-62.dll"
                    "${POCKETFIN_FFMPEG}/bin/avcodec-62.dll"
                    "${POCKETFIN_FFMPEG}/bin/avutil-60.dll"
                    "${POCKETFIN_FFMPEG}/bin/swscale-9.dll"
                    "${POCKETFIN_FFMPEG}/bin/swresample-6.dll"
                    $<TARGET_FILE_DIR:${target}>
            COMMAND_EXPAND_LISTS)
    endif()
endfunction()

pocketfin_ffmpeg(pocketfin-test)


# Built WITHOUT POCKETFIN_CHECKS: the only build of the core with its test
# seams absent.
if(WIN32)
    add_executable(pocketfin-pc
        ${POCKETFIN_SOURCES}
        ${POCKETFIN_DESKTOP_APP}
    )
    target_include_directories(pocketfin-pc PRIVATE ${POCKETFIN_INCLUDES}
        ${stb_SOURCE_DIR})
    target_compile_definitions(pocketfin-pc PRIVATE POCKETFIN_WINDOWED=1
        _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(pocketfin-pc PRIVATE gdi32 ws2_32 winmm)
    pocketfin_ffmpeg(pocketfin-pc)
    if(MSVC)
        target_compile_options(pocketfin-pc PRIVATE /W4 /WX)
    endif()
    # From the repo root, so run/ resolves there rather than beside the binary.
    set_property(TARGET pocketfin-pc PROPERTY
        VS_DEBUGGER_WORKING_DIRECTORY ${REPO_ROOT})
    set_target_properties(pocketfin-pc PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug")
endif()

# stb_image.h does not compile clean at /W4, and libavutil's inline helpers
# narrow ints on purpose. Exempted per file, not per target, so a real warning
# in our own code still fails.
if(MSVC)
    set_source_files_properties(${REPO_ROOT}/src/port/desktop/jpeg.c
        PROPERTIES COMPILE_OPTIONS "/W3")
    set_source_files_properties(${REPO_ROOT}/src/port/desktop/decode.c
        ${REPO_ROOT}/src/port/desktop/audio.c
        PROPERTIES COMPILE_OPTIONS "/W3")
endif()

# The test seams: the card fault in src/base/log.h and the fakes in
# tools/seams.allow.
target_compile_definitions(pocketfin-test PRIVATE POCKETFIN_CHECKS)
if(WIN32)
    target_link_libraries(pocketfin-test PRIVATE ws2_32 winmm)
endif()

if(MSVC)
    # /W4 and warnings as errors: MSVC only WARNS on a signature mismatch
    # (C4029), so a drifted seam compiles quietly without it.
    target_compile_options(pocketfin-test PRIVATE /W4 /WX)
    # fopen and sscanf are what the console has; the _s variants are not
    # portable.
    target_compile_definitions(pocketfin-test PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(pocketfin-test PRIVATE -Wall -Wextra -Werror)
endif()

set_target_properties(pocketfin-test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug")
