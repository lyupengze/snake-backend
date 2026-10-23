cmake_minimum_required(VERSION 3.14)
project(SnakeBackend LANGUAGES CXX)

# ==========================================
# 基础编译配置
# ==========================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# [兼容处理] 高版本 CMake (如 4.x) 对低于 3.5 的依赖会直接报错。
# Crow v1.2.0 内部声明了较低的最低版本，这里设置策略下限以兼容新版 CMake。
set(CMAKE_POLICY_VERSION_MINIMUM "3.5")

include(FetchContent)

# ==========================================
# 拉取 Crow 框架依赖
# ==========================================
# [工程化] 优先检查本地是否存在 Crow-master 源码目录，避免构建时重复拉取网络依赖。
# 如果本地没有，则通过 FetchContent 从 GitHub 拉取指定的 release 版本。
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

# ==========================================
# 平台相关的系统依赖链接
# ==========================================
if(WIN32)
    # [环境适配] 针对本地 Windows (MSYS2 UCRT64) 环境的 Asio 头文件路径进行指定。
    # 注意：如果更换开发环境，需确保此路径下包含 Asio 库。
    set(ASIO_INCLUDE_DIR "D:/msys64/ucrt64/include" CACHE PATH "Path to Asio")

    # Windows 下的网络库 (ws2_32, mswsock) 以及 SQLite3
    set(EXTRA_LIBS ws2_32 mswsock sqlite3)
else()
    # Linux/Unix 环境下的依赖
    find_package(Threads REQUIRED)
    # 链接多线程库、SQLite3 以及动态链接库
    set(EXTRA_LIBS Threads::Threads sqlite3 dl)
endif()

# ==========================================
# 编译目标与选项
# ==========================================
add_executable(server main.cpp GameManager.cpp ReplayEngine.cpp)

# 链接 Crow 及其它系统库
target_link_libraries(server PRIVATE Crow::Crow ${EXTRA_LIBS})

# [规范] 开启严格编译警告，保持代码高质量
target_compile_options(server PRIVATE
    # MSVC 编译器使用 /W4 警告级别
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    # GCC / Clang 编译器开启 -Wall 和 -Wextra
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
)