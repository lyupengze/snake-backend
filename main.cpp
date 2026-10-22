#include <algorithm>
#include <charconv>  // 【新增】用于 C++17 的 std::from_chars
#include <chrono>
#include <ctime>
#include <string_view>
#include <thread>

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

    CROW_ROUTE(app, "/api/daily-seed")
    ([]() {
        std::time_t t = std::time(nullptr);
        std::tm tm_now;
#ifdef _WIN32
        localtime_s(&tm_now, &t);
#else
        localtime_r(&t, &tm_now);
#endif

        int seed = (tm_now.tm_year + 1900) * 10000 + (tm_now.tm_mon + 1) * 100 + tm_now.tm_mday;

        crow::json::wvalue res;
        res["seed"] = seed;
        res["date"] = std::to_string(tm_now.tm_year + 1900) + "-" +
                      std::to_string(tm_now.tm_mon + 1) + "-" + std::to_string(tm_now.tm_mday);
        return crow::response(res);
    });

    CROW_ROUTE(app, "/api/start").methods("POST"_method)([&game_manager]() {
        crow::json::wvalue res;
        res["gameId"] = game_manager.start_game();
        return crow::response(res);
    });

    CROW_ROUTE(app, "/api/score").methods("POST"_method)([&game_manager](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body)
            return crow::response(400, "Invalid JSON");

        // 【修复】修复 ! 优先级核弹，改用 != 判断
        if (!body.has("gameId") || body["gameId"].t() != crow::json::type::String ||
            !body.has("name") || body["name"].t() != crow::json::type::String ||
            !body.has("score") || body["score"].t() != crow::json::type::Number) {
            return crow::response(400, "Missing or invalid fields");
        }

        std::string game_id = body["gameId"].s();
        std::string name = body["name"].s();
        int score = body["score"].i();

        std::string mode = "classic";
        if (body.has("mode") && body["mode"].t() == crow::json::type::String) {
            mode = body["mode"].s();
        }

        if (game_manager.submit_score(game_id, name, score, mode)) {
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