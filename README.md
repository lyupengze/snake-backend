# 🐍 Snake Game C++ Backend

这是一个基于现代 C++17 和 [Crow](https://github.com/CrowCpp/Crow) 框架开发的高性能后端服务，专为 Vue3 贪吃蛇前端提供 API 支持。

## ✨ 核心特性

- **现代 C++17 标准**：使用 `std::mt19937` 替代传统的 `rand()`，使用 `std::mutex` 配合 `std::lock_guard` 保证多线程下的数据竞争安全。
- **服务器权威反作弊**：时间戳由服务器通过 `GameID` 会话生成，严格校验游戏时长与得分速率，拒绝任何前端篡改分数的尝试。
- **防御性编程**：对所有传入的 JSON 数据进行字段存在性、类型及长度校验，防止非法请求导致后端崩溃。
- **容器化部署**：提供多阶段构建的 `Dockerfile`，镜像体积小，开箱即用。

## 📡 API 接口说明

| 方法   | 路径               | 描述                                                                 |
| :----- | :----------------- | :------------------------------------------------------------------- |
| `POST` | `/api/start`       | 开始一局新游戏，返回生成的 `{"gameId": "..."}`                       |
| `POST` | `/api/score`       | 提交分数，Body 格式：`{"gameId": "...", "name": "...", "score": 10}` |
| `GET`  | `/api/leaderboard` | 获取前 10 名的排行榜数据                                             |

## 🛠️ 本地开发 (Windows / Linux)

确保你的系统已安装支持 C++17 的编译器（如 GCC 或 Clang）、CMake 和 Git。

```bash
# 创建构建目录
mkdir build && cd build

# 配置并编译 (Windows 下如需指定 MinGW Makefiles 请自行添加 -G 参数)
cmake ..
cmake --build .

# 运行服务
./server
```

后端将默认监听 `http://localhost:8080`。

## 🐳 Docker 部署

本项目已包含 `Dockerfile`，你可以直接在本地构建并运行：

```bash
# 构建镜像
docker build -t snake-backend .

# 运行容器 (将本地的 8080 映射到容器的 8080)
docker run -d -p 8080:8080 --name my-snake-api snake-backend
```

## ☁️ 云端部署注意事项

在部署到云平台（如 Koyeb, Render, Railway 等）时，请确保平台选择 **Docker** 作为构建环境。由于 C++ 编译需要下载第三方库，首次构建可能会耗时几分钟，请耐心等待。
