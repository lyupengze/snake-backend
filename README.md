# 🐍 Snake Game Backend (C++17 / Crow)

![C++](https://img.shields.io/badge/C++-17-blue.svg)
![Crow](https://img.shields.io/badge/Framework-Crow-green.svg)
![SQLite](https://img.shields.io/badge/Database-SQLite-blue.svg)
![Docker](https://img.shields.io/badge/Deploy-Docker-cyan.svg)
![License](https://img.shields.io/badge/License-MIT-yellow.svg)

这是一个基于 **现代 C++17** 和 **Crow 框架** 开发的高性能 RESTful API 后端服务，为贪吃蛇游戏提供核心数据支持。

本项目不仅实现了常规的业务闭环，更采用了**纯 C++ 编写的服务器权威重放引擎（Server-Side Replay Engine）**，实现了毫秒级的硬核反作弊校验。

## ✨ 核心特性

- **🛡️ 服务器权威重放防作弊**：彻底抛弃不可靠的“前端传啥信啥”，后端内置纯函数式的 `ReplayEngine`。收到分数时，服务器会基于玩家上传的 `seed` 和 `操作记录 (actions)` 在内存中重跑一遍游戏，**分数不一致直接 403 拦截**，物理级杜绝篡改分数的黑客脚本。
- **⚡ 高并发与线程安全**：采用**细粒度锁分离**策略（`session_mtx_` 与 `db_mtx_` 分离），避免大锁吞并发；开启 SQLite `WAL (Write-Ahead Logging)` 模式，实现读写操作完全并发不阻塞。
- **🛡️ 极致的防御性编程**：
  - 使用 `sqlite3_prepare_v2` 与参数绑定，100% 杜绝 SQL 注入。
  - 内置会话 TTL 清理机制，自动清理废弃的 `GameID`，防内存泄漏。
  - 严格限制 `name` 长度与 `actions` 数组大小，防 OOM 攻击。
- **🧩 优雅的架构分层**：彻底分离 Controller 层（路由解析）、Manager 层（业务与数据协调）与 Engine 层（纯游戏逻辑），代码高度内聚。

## 📡 API 接口说明

| 方法   | 路径               | 描述                                                    |
| :----- | :----------------- | :------------------------------------------------------ |
| `GET`  | `/api/daily-seed`  | 获取当天的每日挑战随机数种子                            |
| `POST` | `/api/start`       | 开始游戏，获取专属 `gameId` 与时间戳凭证                |
| `POST` | `/api/score`       | 提交分数，携带 `gameId`, `seed`, `actions` 进行重放校验 |
| `GET`  | `/api/leaderboard` | 获取排行榜（支持 `?mode=classic/daily` 双榜隔离）       |

## 🐳 生产环境部署 (Docker)

采用**多阶段构建**，构建阶段使用完整编译链，运行阶段极简剥离，镜像体积小且安全。

```bash
# 1. 构建镜像
docker build -t snake-backend .

# 2. 启动容器并挂载数据卷 (极其重要，确保 SQLite 数据持久化)
docker run -d \
  --name snake-backend \
  -p 8080:8080 \
  --restart always \
  -v /root/snake_data:/app/data \
  snake-backend
```

## 🧠 架构设计亮点

1. **C++17 现代特性**：使用 `std::mt19937` 替代 `rand()`；使用 `std::from_chars` 实现无异常的高性能数字解析；使用 `localtime_s/r` 解决跨平台时区线程安全问题。
2. **纯逻辑抽离**：`ReplayEngine` 为静态无状态类，不依赖任何网络或数据库组件，可直接进行纯粹的单元测试。
