#include <algorithm>
#include <charconv>

#include "GameManager.h"
#include "crow.h"
#include "crow/middlewares/cors.h"

int main() {
    GameManager game_manager;

    crow::App<crow::CORSHandler> app;
    auto& cors = app.get_middleware<crow::CORSHandler>();
    cors.global()
        .origin("*")
        .methods("POST"_method, "GET"_method, "OPTIONS"_method)
        .headers("Content-Type");

    CROW_ROUTE(app, "/api/start").methods("POST"_method)([&game_manager](const crow::request& req) {
        if (req.body.size() > 1024) {  // 开局请求不可能超过 1KB
            return crow::response(413, "Payload Too Large");
        }
        auto body = crow::json::load(req.body);
        std::string mode = "classic";
        if (body && body.has("mode") && body["mode"].t() == crow::json::type::String) {
            mode = body["mode"].s();
            if (mode != "classic" && mode != "daily") {
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

    CROW_ROUTE(app, "/api/score").methods("POST"_method)([&game_manager](const crow::request& req) {
        if (req.body.size() > 500 * 1024) {
            return crow::response(413, "Payload Too Large");
        }
        auto body = crow::json::load(req.body);
        if (!body)
            return crow::response(400, "Invalid JSON");

        // 【修复】严格的防御性字段校验
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
        for (auto& item : body["actions"]) {
            // 【修复】确保每个 action 里的 tick 和 dir 也是合法的
            if (item.t() == crow::json::type::Object && item.has("tick") && item.has("dir")) {
                if (item["tick"].t() != crow::json::type::Number ||
                    item["dir"].t() != crow::json::type::String) {
                    return crow::response(400, "Invalid action types");
                }
                Action act;
                act.tick = item["tick"].i();
                act.dir = item["dir"].s();
                if (act.dir != "UP" && act.dir != "DOWN" && act.dir != "LEFT" &&
                    act.dir != "RIGHT") {
                    return crow::response(400, "Invalid direction");
                }
                actions.push_back(act);
            } else {
                return crow::response(400, "Invalid action format");
            }
        }

        // 调后端校验
        if (game_manager.submit_score(game_id, name, score, actions)) {
            return crow::response(200, "Score submitted successfully");
        } else {
            return crow::response(403, "Cheating detected or invalid session.");
        }
    });

    CROW_ROUTE(app, "/api/leaderboard")
    ([&game_manager](const crow::request& req) {
        std::string mode = "classic";
        if (req.url_params.get("mode")) {
            mode = req.url_params.get("mode");
            // 【核心修复】防止恶意传参，如果不是合法模式，直接强制回退到经典榜
            if (mode != "classic" && mode != "daily") {
                mode = "classic";
            }
        }

        // ... 下面的 limit 解析和 JSON 组装保持不变 ...

        int limit = 10;
        if (req.url_params.get("limit")) {
            std::string limit_str = req.url_params.get("limit");
            // 【优化】使用 C++17 std::from_chars，性能爆炸且不抛异常
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
            // 【终极修复】直接原地组装，彻底告别拷贝和移动的破事
            result["data"][i]["playerName"] = top[i].player_name;
            result["data"][i]["score"] = top[i].score;
        }

        return crow::response(result);
    });

    app.port(8080).multithreaded().run();
    return 0;
}