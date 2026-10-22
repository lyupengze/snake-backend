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

        // 1. 防御性校验
        if (!body.has("gameId") || !body.has("name") || !body.has("score")) {
            return crow::response(400, "Missing fields");
        }

        std::string game_id = body["gameId"].s();
        std::string name = body["name"].s();
        int score = body["score"].i();

        if (name.empty() || name.length() > 20) {
            return crow::response(400, "Invalid name length");
        }

        // 2. 解析 mode（默认 classic）
        std::string mode = "classic";
        if (body.has("mode")) {
            mode = body["mode"].s();
        }

        // 3. 提交分数（只调用一次，传 4 个参数）
        if (game_manager.submit_score(game_id, name, score, mode)) {
            return crow::response(200, "Score submitted successfully");
        } else {
            return crow::response(403, "Cheating detected or invalid session.");
        }
    });

    // 3. 获取排行榜
    // 获取排行榜
    CROW_ROUTE(app, "/api/leaderboard")
    ([&game_manager](const crow::request& req) {
        // 1. 默认模式为经典模式
        std::string mode = "classic";

        // 2. 如果 URL 里带了 ?mode=daily，就读取出来
        if (req.url_params.get("mode")) {
            mode = req.url_params.get("mode");
        }

        // 3. 去数据库查对应模式的榜单（调用刚才改好的 get_top_scores）
        crow::json::wvalue result;
        auto top = game_manager.get_top_scores(mode, 10);

        // 4. 组装 JSON 返回给前端
        for (size_t i = 0; i < top.size(); ++i) {
            result["data"][i]["playerName"] = top[i].player_name;
            result["data"][i]["score"] = top[i].score;
        }

        return crow::response(result);
    });

    // 获取每日挑战的种子
    CROW_ROUTE(app, "/api/daily-seed")
    ([]() {
        std::time_t t = std::time(nullptr);
        std::tm* now = std::localtime(&t);

        // 将日期转为 YYYYMMDD 作为种子
        int seed = (now->tm_year + 1900) * 10000 + (now->tm_mon + 1) * 100 + now->tm_mday;

        crow::json::wvalue res;
        res["seed"] = seed;
        res["date"] = std::to_string(now->tm_year + 1900) + "-" + std::to_string(now->tm_mon + 1) +
                      "-" + std::to_string(now->tm_mday);
        return crow::response(res);
    });

    app.port(8080).multithreaded().run();
}