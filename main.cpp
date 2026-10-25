#include <algorithm>
#include <charconv>
#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "GameManager.h"
#include "crow.h"

/**
 * 贪吃蛇后端服务入口 (基于 Crow 框架)
 *
 * 负责提供以下核心 API:
 * 1. POST /api/start       - 获取服务器权威 Seed 和 GameID
 * 2. POST /api/score       - 提交游戏分数与操作序列，服务器进行重放验证
 * 3. GET  /api/leaderboard - 获取排行榜数据
 */

// 极简内存限流，防单 IP 高频刷分
struct RateLimiter {
    std::unordered_map<std::string, std::pair<int, std::chrono::steady_clock::time_point>> ips;
    std::mutex mtx;

    bool is_allowed(const std::string& ip) {
        std::lock_guard<std::mutex> lock(mtx);
        auto now = std::chrono::steady_clock::now();
        auto& rec = ips[ip];

        // 如果超过 60 秒，重置计数
        if (std::chrono::duration_cast<std::chrono::seconds>(now - rec.second).count() > 60) {
            rec.first = 1;
            rec.second = now;
            return true;
        }

        // 每分钟限制 30 次
        if (rec.first >= 30)
            return false;
        rec.first++;
        return true;
    }
};
RateLimiter rate_limiter;

int main() {
    GameManager game_manager;

    // [安全修复 P2-1] 删除 CORSHandler 模板参数，使用纯原生 Crow::App
    crow::App<> app;

    // ==========================================
    // 路由 1: 开局请求
    // ==========================================
    CROW_ROUTE(app, "/api/start").methods("POST"_method)([&game_manager](const crow::request& req) {
        // 获取客户端 IP (Crow 默认 req.remote_ip_address)
        std::string client_ip = req.remote_ip_address;
        if (!rate_limiter.is_allowed(client_ip)) {
            return crow::response(429, "Too Many Requests (Crow Layer)");
        }

        auto content_type = req.get_header_value("Content-Type");
        if (content_type.find("application/json") == std::string::npos) {
            return crow::response(415, "Unsupported Media Type");
        }

        // [防御] 限制开局请求体大小，正常开局请求不可能超过 1KB，防止内存消耗攻击
        if (req.body.size() > 1024) {
            return crow::response(413, "Payload Too Large");
        }

        auto body = crow::json::load(req.body);

        // 严格校验 JSON 格式，解析失败直接打回，绝不静默放行
        if (!body) {
            return crow::response(400, "Invalid JSON format");
        }

        std::string mode = "classic";  // 默认模式

        if (body) {
            // 只要包含了 mode 字段，就必须严格校验
            if (body.has("mode")) {
                // 1. 必须且只能是 String 类型
                if (body["mode"].t() != crow::json::type::String) {
                    return crow::response(400, "Invalid mode type");
                }
                mode = body["mode"].s();

                // 2. 严格白名单拦截，绝不静默回退
                if (mode != "classic" && mode != "daily" && mode != "wrap") {
                    return crow::response(400, "Invalid mode");
                }
            }
        }

        auto [gameId, seed] = game_manager.start_game(mode);
        crow::json::wvalue res;
        res["gameId"] = gameId;
        res["seed"] = seed;
        res["mode"] = mode;
        return crow::response(res);
    });

    // ==========================================
    // 路由 2: 提交分数请求 (防作弊前置校验)
    // ==========================================
    CROW_ROUTE(app, "/api/score").methods("POST"_method)([&game_manager](const crow::request& req) {
        // 获取客户端 IP (Crow 默认 req.remote_ip_address)
        std::string client_ip = req.remote_ip_address;
        if (!rate_limiter.is_allowed(client_ip)) {
            return crow::response(429, "Too Many Requests (Crow Layer)");
        }

        auto content_type = req.get_header_value("Content-Type");
        if (content_type.find("application/json") == std::string::npos) {
            return crow::response(415, "Unsupported Media Type");
        }

        if (req.body.size() > 500 * 1024) {
            return crow::response(413, "Payload Too Large");
        }

        auto body = crow::json::load(req.body);
        if (!body) {
            return crow::response(400, "Invalid JSON");
        }

        // [校验 1] 严格防御性字段校验：确保必需字段存在且类型正确
        if (!body.has("gameId") || body["gameId"].t() != crow::json::type::String ||
            !body.has("name") || body["name"].t() != crow::json::type::String ||
            !body.has("score") || body["score"].t() != crow::json::type::Number ||
            !body.has("actions") || body["actions"].t() != crow::json::type::List) {
            return crow::response(400, "Missing or invalid fields");
        }

        std::string game_id = body["gameId"].s();
        if (game_id.length() != 16) {
            CROW_LOG_WARNING << "蓝队审计: 拦截非法 GameID 长度: " << game_id;
            return crow::response(403, "Cheating detected or invalid session.");
        }

        std::string name = body["name"].s();

        // 1. 先过滤掉玩家手滑输入的前后空格，防止分数分家
        if (!name.empty() && name.front() == ' ') {
            name.erase(0, name.find_first_not_of(' '));
        }
        if (!name.empty() && name.back() == ' ') {
            name.erase(name.find_last_not_of(' ') + 1);
        }

        // 2. 精确的长度与空值校验
        if (name.empty() || name.length() > GameConfig::kMaxNameLength) {
            return crow::response(400, "Name too long or empty");
        }

        // 彻底放弃 std::isalnum（受 Locale 影响不稳定）
        // 改用纯 ASCII 手动判断 + 直接放行所有 UTF-8 中文字节 (c >= 0x80)
        for (unsigned char c : name) {
            bool is_ascii_alnum =
                (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');

            if (!(is_ascii_alnum || c == '_' || c == '-' || c >= 0x80)) {
                return crow::response(400, "Invalid character in name");
            }
        }

        int score = body["score"].i();

        std::vector<Action> actions;
        // [校验 3] 深度校验每个 action 的结构与内容合法性
        for (auto& item : body["actions"]) {
            if (item.t() == crow::json::type::Object && item.has("tick") && item.has("dir")) {
                if (item["tick"].t() != crow::json::type::Number ||
                    item["dir"].t() != crow::json::type::String) {
                    return crow::response(400, "Invalid action types");
                }

                Action act;
                act.tick = item["tick"].i();
                act.dir = item["dir"].s();

                // 方向指令必须符合白名单
                if (act.dir != "UP" && act.dir != "DOWN" && act.dir != "LEFT" &&
                    act.dir != "RIGHT") {
                    return crow::response(400, "Invalid direction");
                }
                actions.push_back(act);
            } else {
                return crow::response(400, "Invalid action format");
            }
        }

        // 路由层校验全部通过，交由业务层 (GameManager) 进行权威重放与数据库写入
        SubmitResult result = game_manager.submit_score(game_id, name, score, actions);
        switch (result) {
            case SubmitResult::Success:
                return crow::response(200, "Score submitted successfully");
            case SubmitResult::InvalidData:
                return crow::response(400, "Invalid data (e.g., name too long)");
            case SubmitResult::PayloadTooLarge:
                return crow::response(413, "Payload Too Large (game too long)");
            case SubmitResult::Cheating:
                return crow::response(403, "Cheating detected or invalid session.");
            case SubmitResult::InternalError:
            default:
                return crow::response(500, "Internal Server Error");
        }
    });

    // ==========================================
    // 路由 3: 获取排行榜
    // ==========================================
    CROW_ROUTE(app, "/api/leaderboard")
    ([&game_manager](const crow::request& req) {
        std::string mode = "classic";

        // [校验] 解析 mode 参数，白名单机制防止恶意传参
        if (req.url_params.get("mode")) {
            mode = req.url_params.get("mode");

            // 严格白名单，不再静默回退
            if (mode != "classic" && mode != "daily" && mode != "wrap") {
                CROW_LOG_WARNING << "蓝队审计: 拦截非法 mode: " << mode;
                return crow::response(400, "Invalid mode");
            }
        } else {
            // 缺失 mode 参数时，不再静默使用默认值，而是直接拒绝
            return crow::response(400, "Missing mode parameter");
        }

        int limit = 10;  // 默认展示前 10 名
        if (req.url_params.get("limit")) {
            std::string limit_str = req.url_params.get("limit");
            // [性能] 使用 C++17 std::from_chars 进行无异常、零分配的高性能整型解析
            auto [ptr, ec] =
                std::from_chars(limit_str.data(), limit_str.data() + limit_str.size(), limit);
            if (ec != std::errc()) {
                limit = 10;  // 解析失败退回默认值
            }
        }

        crow::json::wvalue result;
        auto top = game_manager.get_top_scores(mode, limit);

        result["data"] = crow::json::wvalue::list();
        for (size_t i = 0; i < top.size(); ++i) {
            result["data"][i]["playerName"] = top[i].player_name;
            result["data"][i]["score"] = top[i].score;
        }

        return crow::response(result);
    });

    // [安全修复 P2-2] 全局 404 兜底放必须在所有精确路由之后！
    CROW_ROUTE(app, "/<path>")
        .methods("GET"_method, "POST"_method, "PUT"_method,
                 "DELETE"_method)([](const crow::request&, std::string) {
            return crow::response(404, "{\"error\": \"Route Not Found\"}");
        });

    // 启动 HTTP 服务，监听 8080 端口，开启多线程模式
    // 仅绑定 127.0.0.1，防止局域网内其他设备直接绕过 Nginx 访问后端
    app.bindaddr("127.0.0.1").port(8080).multithreaded().run();
    return 0;
}