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

    // 1. 开始游戏
    CROW_ROUTE(app, "/api/start").methods("POST"_method)([&game_manager]() {
        crow::json::wvalue res;
        res["gameId"] = game_manager.start_game();  // 输出给前端用 camelCase
        return crow::response(res);
    });

    // 2. 提交分数（防御性解析）
    CROW_ROUTE(app, "/api/score").methods("POST"_method)([&game_manager](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body)
            return crow::response(400, "Invalid JSON");

        // 防御性校验，防止 Crow 抛异常
        if (!body.has("gameId") || !body.has("name") || !body.has("score")) {
            return crow::response(400, "Missing fields");
        }

        std::string game_id = body["gameId"].s();
        std::string name = body["name"].s();
        int score = body["score"].i();

        if (name.empty() || name.length() > 20) {
            return crow::response(400, "Invalid name length");
        }

        if (game_manager.submit_score(game_id, name, score)) {
            return crow::response(200, "Score submitted successfully");
        } else {
            return crow::response(403, "Cheating detected or invalid session.");
        }
    });

    // 3. 获取排行榜
    CROW_ROUTE(app, "/api/leaderboard")
    ([&game_manager]() {
        crow::json::wvalue result;
        auto top = game_manager.get_top_scores(10);
        for (size_t i = 0; i < top.size(); ++i) {
            // C++ 内用 snake_case，输出给前端转成 camelCase
            result["data"][i]["playerName"] = top[i].player_name;
            result["data"][i]["score"] = top[i].score;
        }
        return crow::response(result);
    });

    app.port(8080).multithreaded().run();
}