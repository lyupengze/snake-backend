cmake_minimum_required(VERSION 3.14)
project(SnakeBackend LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Crow v1.2.0 声明了一个较低的最低策略版本；此行防止 CMake 4.x 在配置阶段拒绝它。
set(CMAKE_POLICY_VERSION_MINIMUM "3.5")

include(FetchContent)

# 优先使用本地 Crow 源码目录（Crow-master/），避免构建时的网络拉取。
# 若本地目录不存在，则回退到从 GitHub 下载。
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

if(WIN32)
    # 若 Asio 头文件位于其他路径（如 MSYS2 UCRT64），请修改此处。
    # 必须在 FetchContent_MakeAvailable 之前设置，否则 Crow 配置时找不到 Asio。
    set(ASIO_INCLUDE_DIR "D:/msys64/ucrt64/include" CACHE PATH "Path to Asio")
endif()

FetchContent_MakeAvailable(crow)

if(WIN32)
    set(EXTRA_LIBS ws2_32 mswsock sqlite3)
else()
    find_package(Threads REQUIRED)
    set(EXTRA_LIBS Threads::Threads sqlite3 dl)
endif()

add_executable(server main.cpp GameManager.cpp ReplayEngine.cpp)

target_link_libraries(server PRIVATE Crow::Crow ${EXTRA_LIBS})

target_compile_options(server PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
)
