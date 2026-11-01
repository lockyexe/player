# Host-side tests for the platform-independent core (parser + scheduler).
# Not part of the mod build. Run:
#   cmake -S tests -B build-tests && cmake --build build-tests
#   ./build-tests/gdr2_generate build-tests/fixtures
#   ./build-tests/macro_tests build-tests/fixtures path/to/real/macros
cmake_minimum_required(VERSION 3.21)
project(MacroPlayerTests CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(macro_tests
    macro_tests.cpp
    ../src/core/MacroParser.cpp
)
target_compile_options(macro_tests PRIVATE -Wall -Wextra -fsanitize=address,undefined)
target_link_options(macro_tests PRIVATE -fsanitize=address,undefined)

# Official reference implementation of the format, used to generate GDR2
# fixtures and to dump what the reference reader sees in GDR1 files.
include(FetchContent)
FetchContent_Declare(gdr2_ref
    GIT_REPOSITORY https://github.com/maxnut/GDReplayFormat.git
    GIT_TAG d8d2f0b1193a46cdf47b413dc832616dd052b6a9 # gdr2 branch
)
FetchContent_Declare(gdr1_ref
    GIT_REPOSITORY https://github.com/maxnut/GDReplayFormat.git
    GIT_TAG 77bd505853541a87a12895dc23de05f6585d0515 # gdr1 branch
)
FetchContent_GetProperties(gdr2_ref)
if(NOT gdr2_ref_POPULATED)
    FetchContent_Populate(gdr2_ref)
endif()
FetchContent_GetProperties(gdr1_ref)
if(NOT gdr1_ref_POPULATED)
    FetchContent_Populate(gdr1_ref)
endif()

add_executable(gdr2_generate reference/gdr2_generate.cpp)
target_include_directories(gdr2_generate PRIVATE ${gdr2_ref_SOURCE_DIR}/include)

add_executable(gdr1_dump reference/gdr1_dump.cpp)
target_include_directories(gdr1_dump PRIVATE ${gdr1_ref_SOURCE_DIR}/include)
