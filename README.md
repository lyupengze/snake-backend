# 🐍 Snake Game Backend (C++17 / Crow)

![C++](https://img.shields.io/badge/C++-17-blue.svg)
![Crow](https://img.shields.io/badge/Framework-Crow-green.svg)
![SQLite](https://img.shields.io/badge/Database-SQLite-blue.svg)
![Docker](https://img.shields.io/badge/Deploy-Docker-cyan.svg)
![License](https://img.shields.io/badge/License-MIT-yellow.svg)

这是一个基于 **现代 C++17** 和 **Crow 框架** 开发的高性能 RESTful API 后端服务。本项目不仅提供了常规的双榜单（经典/每日）排行榜支持，更实现了一套**基于服务器权威的硬核防作弊重放引擎（Server-Authoritative Replay Engine）**。

## ✨ 核心特性

- **🛡️ 服务器权威防作弊 (Anti-Cheat)**：
  - **绝对信任隔离**：游戏开局时，由服务器生成并下发权威的随机数种子（`seed`），并绑定在 `Session` 中。
  - **时间悖论校验**：基于操作记录的最大 `tick` 计算出预期耗时。若物理耗时远小于预期耗时（如写脚本瞬间伪造 1000 帧操作），直接判定作弊并拦截。
  - **状态重放引擎**：收到成绩时，忽略前端传入的任何状态，使用服务器下发的 `seed` 和前端上传的 `actions` 在 C++ 内存中极速重跑游戏。**计算分数不一致直接 403 踢飞**。
- **⚡ 高并发与线程安全**：
  - **细粒度锁分离**：使用 `session_mtx_` 与 `db_mtx_` 分离锁，防止高频提交时的锁竞争。
  - **无锁重放**：在重放校验阶段提前释放 Session 锁，确保长时间的计算不阻塞其他玩家开局。
  - 开启 SQLite `WAL (Write-Ahead Logging)` 模式，实现真正的读写并发。
- **🛡️ 极致的防御性编程**：
  - 使用 `sqlite3_prepare_v2` 与参数绑定，100% 杜绝 SQL 注入。
  - 严格进行入参校验（字段类型、方向白名单、`name` 长度限制），防止 OOM 和非法数据。
  - 内置 TTL 会话清理机制，自动清理“开局即跑路”的废弃 `GameID`。
- **🧩 模块化架构**：
  - `Controller` 层（`main.cpp`）负责路由与 JSON 解析。
  - `Manager` 层（`GameManager`）负责状态、校验与数据持久化。
  - `Engine` 层（`ReplayEngine`）为纯函数静态类，零外部依赖，便于单元测试。

## 📡 API 接口说明

| 方法   | 路径               | 描述                                                                |
| :----- | :----------------- | :------------------------------------------------------------------ |
| `POST` | `/api/start`       | 传入 `{"mode": "classic/daily"}`，返回专属的 `gameId` 和权威 `seed` |
| `POST` | `/api/score`       | 提交分数，携带 `gameId`, `name`, `score`, `actions` 进行重放校验    |
| `GET`  | `/api/leaderboard` | 获取排行榜（支持 `?mode=classic/daily` 双榜隔离与 `limit` 限制）    |

## 🐳 生产环境部署 (Docker)

采用**多阶段构建**，构建阶段使用完整编译链，运行阶段极简剥离，镜像体积小且安全。

```bash
# 1. 构建镜像
docker build -t snake-backend .

# 2. 启动容器并挂载数据卷 (极其重要，确保 SQLite 数据持久化)
# 注意：线上使用的是 Docker named volume `snake_data_volume`，
# 不是早期版本里的 bind mount `/root/snake_data`。两者不可混用。
docker run -d \
  --name snake-backend \
  -p 127.0.0.1:8080:8080 \
  --restart always \
  -v snake_data_volume:/app/data \
  snake-backend
```

## 🧠 架构设计亮点

1. **现代 C++17 特性**：使用 `std::mt19937` 替代 `rand()`，生成高质量随机种子；使用 `std::from_chars` 实现无异常的高性能数字解析；使用 `localtime_s/r` 解决跨平台时区线程安全问题。
2. **跨日结算兼容**：在开局时固化 `Session.date`，即使玩家在 23:59 开局、00:01 提交，成绩依然精准落入昨日榜单。
