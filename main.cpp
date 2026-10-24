#include <algorithm>
#include <charconv>
#include <string>
#include <vector>

#include "GameManager.h"
#include "crow.h"
#include "crow/middlewares/cors.h"

/**
 * 贪吃蛇后端服务入口 (基于 Crow 框架)
 *
 * 负责提供以下核心 API:
 * 1. POST /api/start       - 获取服务器权威 Seed 和 GameID
 * 2. POST /api/score       - 提交游戏分数与操作序列，服务器进行重放验证
 * 3. GET  /api/leaderboard - 获取排行榜数据
 */
int main() {
    GameManager game_manager;

    crow::App<crow::CORSHandler> app;
    auto& cors = app.get_middleware<crow::CORSHandler>();
    // 全局 CORS 配置，允许所有来源跨域请求
    cors.global()
        .origin("*")
        .methods("POST"_method, "GET"_method, "OPTIONS"_method)
        .headers("Content-Type");

    CROW_ROUTE(app, "/<path>")
        .methods("GET"_method, "POST"_method, "PUT"_method,
                 "DELETE"_method)([](const crow::request&, std::string) {
            return crow::response(404, "{\"error\": \"Route Not Found\"}");
        });

    // ==========================================
    // 路由 1: 开局请求
    // ==========================================
    CROW_ROUTE(app, "/api/start").methods("POST"_method)([&game_manager](const crow::request& req) {
        auto content_type = req.get_header_value("Content-Type");
        if (content_type.find("application/json") == std::string::npos) {
            return crow::response(415, "Unsupported Media Type");
        }

        // [防御] 限制开局请求体大小，正常开局请求不可能超过 1KB，防止内存消耗攻击
        if (req.body.size() > 1024) {
            return crow::response(413, "Payload Too Large");
        }

        auto body = crow::json::load(req.body);
        std::string mode = "classic";  // 默认模式

        // 解析并校验 mode 参数，非白名单内的非法参数强制回退为经典模式
        if (body && body.has("mode") && body["mode"].t() == crow::json::type::String) {
            mode = body["mode"].s();
            // [修改] 放行 "wrap" 模式
            if (mode != "classic" && mode != "daily" && mode != "wrap") {
                mode = "classic";
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
            return crow::response(400, "Invalid game ID");
        }

        std::string name = body["name"].s();
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
            // [安全修复 P3-3] 严格白名单，不再静默回退
            if (mode != "classic" && mode != "daily" && mode != "wrap") {
                return crow::response(400, "Invalid mode");
            }
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

    // 启动 HTTP 服务，监听 8080 端口，开启多线程模式
    app.port(8080).multithreaded().run();
    return 0;
}