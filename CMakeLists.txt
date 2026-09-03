cmake_minimum_required(VERSION 3.20)

project(IRCClient
  VERSION 0.1.0
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer (GCC/Clang)" OFF)
option(ENABLE_COVERAGE "Enable GCC/Clang coverage instrumentation" OFF)

if(ENABLE_SANITIZERS AND ENABLE_COVERAGE)
  message(FATAL_ERROR "ENABLE_SANITIZERS and ENABLE_COVERAGE cannot be enabled in the same build")
endif()

if(ENABLE_SANITIZERS AND NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  message(FATAL_ERROR "ENABLE_SANITIZERS requires GCC or Clang")
endif()

if(ENABLE_COVERAGE AND NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  message(FATAL_ERROR "ENABLE_COVERAGE requires GCC or Clang")
endif()

function(ircclient_set_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endfunction()

function(ircclient_apply_instrumentation target)
  if(ENABLE_SANITIZERS)
    target_compile_options(${target} PRIVATE
      -fsanitize=address,undefined
      -fno-omit-frame-pointer
      -g
    )
    target_link_options(${target} PRIVATE
      -fsanitize=address,undefined
    )
  endif()

  if(ENABLE_COVERAGE)
    target_compile_options(${target} PRIVATE
      --coverage
      -fprofile-arcs
      -ftest-coverage
    )
    target_link_options(${target} PRIVATE --coverage)
  endif()
endfunction()

file(GLOB ircclient_core_sources CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)
list(FILTER ircclient_core_sources EXCLUDE REGEX "Main\\.cpp$")

add_library(ircclient_core STATIC ${ircclient_core_sources})
target_include_directories(ircclient_core
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(ircclient_core PUBLIC Threads::Threads)

if(WIN32)
  target_link_libraries(ircclient_core PUBLIC ws2_32)
endif()

ircclient_set_warnings(ircclient_core)
ircclient_apply_instrumentation(ircclient_core)

add_executable(ircclient "${CMAKE_CURRENT_SOURCE_DIR}/src/Main.cpp")
target_link_libraries(ircclient PRIVATE ircclient_core)
set_target_properties(ircclient PROPERTIES OUTPUT_NAME ircclient)
ircclient_set_warnings(ircclient)
ircclient_apply_instrumentation(ircclient)

include(FetchContent)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG 317ac1ed4c0bb6e6b91eafc817e05c488feffcb3 # v3.16.0
)
set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
set(CATCH_INSTALL_EXTRAS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(Catch2)
list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")

include(CTest)
add_subdirectory(tests)
