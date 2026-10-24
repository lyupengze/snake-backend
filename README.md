# 🐍 Snake Battle Backend (C++17 / Crow)

![C++](https://img.shields.io/badge/C++-17-blue.svg)
![Crow](https://img.shields.io/badge/Framework-Crow-green.svg)
![SQLite](https://img.shields.io/badge/Database-SQLite-blue.svg)
![Docker](https://img.shields.io/badge/Deploy-Docker-cyan.svg)
![License](https://img.shields.io/badge/License-MIT-yellow.svg)

这是 **贪吃蛇大作战 (Snake Battle)** 的高性能后端服务。基于现代 C++17 和 Crow 框架开发，核心提供**全模式（经典/每日/穿墙）**的排行榜支持，并内置了一套硬核的**基于服务器权威的防作弊重放引擎**。

## ✨ 核心特性

- **🛡️ 服务器权威防作弊 (Anti-Cheat)**：
  - **状态重放引擎**：收到成绩时，忽略前端状态，使用服务器下发的 `seed` 和前端上传的 `actions` 在 C++ 内存中极速重跑游戏。**计算分数不一致直接 403 拦截**。
  - **多模式物理同步**：完美支持经典模式（撞墙死）与穿墙模式（Wrap-around，坐标循环），保证不同玩法下的重放校验绝对准确。
  - **时间悖论校验**：基于操作记录的最大 `tick` 计算预期耗时。若物理耗时远小于预期，直接判定作弊。
  - **全链路拦截日志**：所有防作弊拦截（如伪造 ID、乱序 tick、分数不符等）均会输出详细的中文 `std::cerr` 日志。
- **⚡ 高并发与线程安全**：
  - **细粒度锁分离**：使用 `session_mtx_` 与 `db_mtx_` 分离锁，防止高频提交时的锁竞争。
  - 开启 SQLite `WAL (Write-Ahead Logging)` 模式，实现真正的读写并发。
- **🛡️ 极致的防御性编程**：
  - 使用 `sqlite3_prepare_v2` 与参数绑定，100% 杜绝 SQL 注入。
  - 严格进行入参校验（16位 GameID 校验、方向白名单、`name` 长度限制），防 OOM 和非法数据。
  - 内置 TTL 会话清理机制，自动清理“开局即跑路”的废弃 `GameID`。
- **🚀 智能自动化部署**：
  - 自带 `deploy.sh` 脚本，支持**源码指纹校验**（源码未变跳过构建）和**自动回滚机制**（新容器启动失败则秒级切回旧镜像）。

## 📡 API 接口说明

| 方法   | 路径               | 描述                                                                     |
| :----- | :----------------- | :----------------------------------------------------------------------- |
| `POST` | `/api/start`       | 传入 `{"mode": "classic/daily/wrap"}`，返回专属的 `gameId` 和权威 `seed` |
| `POST` | `/api/score`       | 提交分数，携带 `gameId`, `name`, `score`, `actions` 进行重放校验         |
| `GET`  | `/api/leaderboard` | 获取排行榜（支持 `?mode=classic/daily/wrap` 多榜隔离与 `limit` 限制）    |

## 🐳 生产环境部署 (Docker)

采用**多阶段构建**，运行阶段极简剥离。

```bash
# 1. 构建镜像
docker build -t snake-backend .

# 2. 启动容器并挂载数据卷
docker run -d \
  --name my-snake \
  -p 127.0.0.1:8080:8080 \
  --restart always \
  -v snake_data_volume:/app/data \
  snake-backend
```

_(注：线上推荐使用自带的一键部署脚本 `./deploy.sh`，它会自动处理拉取、构建和回滚)_

## 🧠 架构设计亮点

1. **现代 C++17 特性**：使用 `std::mt19937` 生成高质量随机种子；`std::from_chars` 实现无异常的高性能数字解析。
2. **跨日结算兼容**：在开局时固化 `Session.date`，即使玩家在 23:59 开局、00:01 提交，成绩依然精准落入昨日榜单。
3. **UPSERT 语句**：使用 SQLite 的 `ON CONFLICT DO UPDATE`，只有当新分数高于历史分数时才更新排行榜，保证高分不被低分覆盖。
