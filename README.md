# 🐍 Snake Game Backend (C++17 / Crow)

![C++](https://img.shields.io/badge/C++-17-blue.svg)
![Crow](https://img.shields.io/badge/Framework-Crow-green.svg)
![SQLite](https://img.shields.io/badge/Database-SQLite-blue.svg)
![Docker](https://img.shields.io/badge/Deploy-Docker-cyan.svg)

这是一个基于 **现代 C++17** 和 **Crow 框架** 开发的高性能 RESTful API 后端服务，专为贪吃蛇游戏（Vue3 前端）提供核心数据支持。

## ✨ 核心特性

- **极速编译与运行**：基于 `C++17` 纯手写业务逻辑，抛弃冗余依赖，应用原生 `SQLite3` C 接口进行数据持久化。
- **高并发与线程安全**：采用**细粒度锁分离**策略（`session_mtx_` 与 `db_mtx_`），并开启 SQLite `WAL (Write-Ahead Logging)` 模式，实现读写操作完全并发不阻塞。
- **服务器权威防作弊**：
  - 采用 `GameID` 会话机制下发，严格校验游戏时长（防瞬间爆分）。
  - 采用 `SQLite 预编译 (sqlite3_prepare_v2)` 与参数绑定，100% 杜绝 SQL 注入风险。
- **每日挑战防重放**：配合前端通过同一时间种子（Seed），保证全网玩家每日食物生成序列绝对一致。
- **防御性编程**：内置会话 TTL 清理机制（自动清理废弃请求）、入参边界校验（防 OOM 攻击）。

## 📡 API 接口说明

| 方法   | 路径               | 描述                                                  |
| :----- | :----------------- | :---------------------------------------------------- |
| `GET`  | `/api/daily-seed`  | 获取当天的每日挑战随机数种子                          |
| `POST` | `/api/start`       | 开始游戏，获取专属 `gameId`                           |
| `POST` | `/api/score`       | 提交分数（含 `gameId`, `mode`），服务端执行反作弊校验 |
| `GET`  | `/api/leaderboard` | 获取排行榜（支持 `?mode=classic/daily` 参数隔离）     |

## 🛠️ 本地开发与运行

### 依赖环境

- GCC 9+ / Clang 7+ (支持 C++17)
- CMake 3.14+
- SQLite3

### 编译与启动

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
./server
```

## 🐳 生产环境部署 (Docker)

采用**多阶段构建**，镜像体积更小，运行更安全。使用 Docker Volume 实现数据库文件持久化。

```bash
# 1. 构建镜像
docker build -t snake-backend .

# 2. 启动容器并挂载数据盘 (极其重要，防止数据丢失)
docker run -d \
  --name my-snake \
  -p 8080:8080 \
  --restart always \
  -v /root/snake_data:/app/data \
  snake-backend
```

## 🧠 架构设计亮点

1. **面向对象封装**：将 `Session` 生命周期与 `Score` 校验逻辑完全封装在 `GameManager` 中，并在 `main.cpp` 保证单例引用传递，杜绝深拷贝。
2. **C++17 现代特性**：利用 `std::mt19937` 替代 `rand()`，配合 `std::uniform_int_distribution` 生成高安全随机序列；使用 `std::from_chars` 实现无异常的高性能字符串解析。
3. **跨平台兼容**：精准使用 `_WIN32` 宏与 `localtime_r/s` 适配 Linux 和 Windows 的时区处理。
