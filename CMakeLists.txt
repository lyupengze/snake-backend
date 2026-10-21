cmake_minimum_required(VERSION 3.14)
project(SnakeBackend LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POLICY_VERSION_MINIMUM "3.5")

include(FetchContent)

# 【关键魔法】检测本地是否存在 Crow-master 文件夹
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Crow-master")
    message(STATUS "Using local Crow source directory.")
    FetchContent_Declare(
        crow
        SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Crow-master"
    )
else()
    message(STATUS "Local Crow not found. Downloading from GitHub...")
    FetchContent_Declare(
        crow
        GIT_REPOSITORY https://github.com/CrowCpp/Crow.git
        GIT_TAG v1.2.0
    )
endif()

FetchContent_MakeAvailable(crow)

# 区分平台处理依赖
if(WIN32)
    set(ASIO_INCLUDE_DIR "D:/msys64/ucrt64/include" CACHE PATH "Path to Asio")
    set(EXTRA_LIBS ws2_32 mswsock)
else()
    find_package(Threads REQUIRED)
    set(EXTRA_LIBS Threads::Threads)
endif()

add_executable(server main.cpp GameManager.cpp)

target_link_libraries(server PRIVATE Crow::Crow ${EXTRA_LIBS})

# 开启严格编译警告
target_compile_options(server PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
)