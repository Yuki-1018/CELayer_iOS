cmake_minimum_required(VERSION 3.20)
project(CELayerCore VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_library(CELayerCore STATIC
    Sources/Core/CEVirtualMemory.c
    Sources/Core/CEPELoader.c
    Sources/Core/CECPU.c
    Sources/Core/CEKernel.c
    Sources/Core/CEWindowServer.c
    Sources/Core/CERuntime.c
)
target_include_directories(CELayerCore PUBLIC Sources/Core/include)
target_compile_options(CELayerCore PRIVATE -Wall -Wextra -Wpedantic -Werror)

include(CTest)
if(BUILD_TESTING)
  add_executable(CELayerCoreTests Tests/CoreTests.c)
  target_link_libraries(CELayerCoreTests PRIVATE CELayerCore)
  target_compile_options(CELayerCoreTests PRIVATE -Wall -Wextra -Wpedantic -Werror)
  add_test(NAME CELayerCoreTests COMMAND CELayerCoreTests)
endif()
