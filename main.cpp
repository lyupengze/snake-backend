#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "GameManager.h"
#include "crow.h"

// API 路由：
//   POST /api/start       — 创建游戏会话，返回 gameId + 权威种子
//   POST /api/score       — 提交分数，附带操作回放供反作弊校验
//   GET  /api/leaderboard — 按模式查询排行榜

static const std::string INTERNAL_SECRET = []() {
    const char* s = std::getenv("INTERNAL_SECRET");
    return s ? std::string(s) : std::string();
}();

struct RateLimiter {
    std::unordered_map<std::string, std::pair<int, std::chrono::steady_clock::time_point>> ips;
    std::mutex mtx;
    std::chrono::steady_clock::time_point last_cleanup = std::chrono::steady_clock::now();

    void cleanup(std::chrono::steady_clock::time_point now) {
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_cleanup).count() < 60)
            return;
        last_cleanup = now;

        for (auto it = ips.begin(); it != ips.end();) {
            if (std::chrono::duration_cast<std::chrono::seconds>(now - it->second.second).count() >
                60)
                it = ips.erase(it);
            else
                ++it;
        }
    }

    bool is_allowed(const std::string& ip, int limit) {
        std::lock_guard<std::mutex> lock(mtx);
        auto now = std::chrono::steady_clock::now();
        cleanup(now);

        auto& rec = ips[ip];

        if (std::chrono::duration_cast<std::chrono::seconds>(now - rec.second).count() > 60) {
            rec.first = 1;
            rec.second = now;
            return true;
        }

        if (rec.first >= limit)
            return false;
        rec.first++;
        return true;
    }
};
RateLimiter write_rate_limiter;
RateLimiter read_rate_limiter;

// 与 Nginx 限流参数对齐：写接口 30r/m，读接口 120r/m
constexpr int RATE_LIMIT_WRITE = 30;
constexpr int RATE_LIMIT_READ = 120;

int main() {
    GameManager game_manager;
    crow::App<> app;

    CROW_ROUTE(app, "/api/start").methods("POST"_method)([&game_manager](const crow::request& req) {
        if (INTERNAL_SECRET.empty() ||
            req.get_header_value("X-Internal-Secret") != INTERNAL_SECRET) {
            CROW_LOG_WARNING << "Blocked unauthorized direct access (IP: "
                             << req.remote_ip_address << ")";
            return crow::response(403, "Forbidden: Direct access not allowed");
        }

        std::string client_ip = req.get_header_value("X-Real-IP");
        if (client_ip.empty()) {
            client_ip = req.remote_ip_address;
        }

        if (!write_rate_limiter.is_allowed(client_ip, RATE_LIMIT_WRITE)) {
            crow::response res(429, "{\"error\":\"Too Many Requests\",\"retryAfter\":60}");
            res.add_header("Retry-After", "60");
            res.add_header("Content-Type", "application/json");
            return res;
        }

        auto content_type = req.get_header_value("Content-Type");
        if (content_type.find("application/json") == std::string::npos) {
            return crow::response(415, "Unsupported Media Type");
        }

        if (req.body.size() > 1024) {
            return crow::response(413, "Payload Too Large");
        }

        auto body = crow::json::load(req.body);
        if (!body) {
            return crow::response(400, "Invalid JSON format");
        }

        std::string mode = "classic";
        if (body.has("mode")) {
            if (body["mode"].t() != crow::json::type::String) {
                return crow::response(400, "Invalid mode type");
            }
            mode = body["mode"].s();
            if (mode != "classic" && mode != "daily" && mode != "wrap") {
                return crow::response(400, "Invalid mode");
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
        if (INTERNAL_SECRET.empty() ||
            req.get_header_value("X-Internal-Secret") != INTERNAL_SECRET) {
            CROW_LOG_WARNING << "Blocked unauthorized direct access (IP: "
                             << req.remote_ip_address << ")";
            return crow::response(403, "Forbidden: Direct access not allowed");
        }

        std::string client_ip = req.get_header_value("X-Real-IP");
        if (client_ip.empty()) {
            client_ip = req.remote_ip_address;
        }

        if (!write_rate_limiter.is_allowed(client_ip, RATE_LIMIT_WRITE)) {
            crow::response res(429, "{\"error\":\"Too Many Requests\",\"retryAfter\":60}");
            res.add_header("Retry-After", "60");
            res.add_header("Content-Type", "application/json");
            return res;
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

        if (!body.has("gameId") || body["gameId"].t() != crow::json::type::String ||
            !body.has("name") || body["name"].t() != crow::json::type::String ||
            !body.has("score") || body["score"].t() != crow::json::type::Number ||
            !body.has("actions") || body["actions"].t() != crow::json::type::List) {
            return crow::response(400, "Missing or invalid fields");
        }

        std::string game_id = body["gameId"].s();
        if (game_id.length() != 16) {
            CROW_LOG_WARNING << "Blocked invalid GameID length: " << game_id;
            return crow::response(403, "Cheating detected or invalid session.");
        }

        std::string name = body["name"].s();

        if (!name.empty() && name.front() == ' ') {
            name.erase(0, name.find_first_not_of(' '));
        }
        if (!name.empty() && name.back() == ' ') {
            name.erase(name.find_last_not_of(' ') + 1);
        }

        if (name.empty() || name.length() > GameConfig::kMaxNameLength) {
            return crow::response(400, "Name too long or empty");
        }

        // 纯 ASCII 字母数字检查 + 允许 UTF-8 续字节（>= 0x80）以支持中日韩字符。
        // 避免使用依赖 locale 的 std::isalnum。
        for (unsigned char c : name) {
            bool is_ascii_alnum =
                (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
            if (!(is_ascii_alnum || c == '_' || c == '-' || c >= 0x80)) {
                return crow::response(400, "Invalid character in name");
            }
        }

        int score = body["score"].i();

        std::vector<Action> actions;
        for (auto& item : body["actions"]) {
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

    CROW_ROUTE(app, "/api/leaderboard")
    ([&game_manager](const crow::request& req) {
        std::string client_ip = req.get_header_value("X-Real-IP");
        if (client_ip.empty()) {
            client_ip = req.remote_ip_address;
        }

        if (!read_rate_limiter.is_allowed(client_ip, RATE_LIMIT_READ)) {
            crow::response res(429, "{\"error\":\"Too Many Requests\",\"retryAfter\":60}");
            res.add_header("Retry-After", "60");
            res.add_header("Content-Type", "application/json");
            return res;
        }

        std::string mode = "classic";

        if (req.url_params.get("mode")) {
            mode = req.url_params.get("mode");
            if (mode != "classic" && mode != "daily" && mode != "wrap") {
                CROW_LOG_WARNING << "Blocked invalid mode: " << mode;
                return crow::response(400, "Invalid mode");
            }
        } else {
            return crow::response(400, "Missing mode parameter");
        }

        int limit = 10;
        if (req.url_params.get("limit")) {
            std::string limit_str = req.url_params.get("limit");
            auto [ptr, ec] =
                std::from_chars(limit_str.data(), limit_str.data() + limit_str.size(), limit);
            if (ec != std::errc()) {
                limit = 10;
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

    // 兜底 404 — 必须在所有具体路由之后注册。
    CROW_ROUTE(app, "/<path>")
        .methods("GET"_method, "POST"_method, "PUT"_method,
                 "DELETE"_method)([](const crow::request&, std::string) {
            return crow::response(404, "{\"error\": \"Route Not Found\"}");
        });

    app.bindaddr("0.0.0.0").port(8080).multithreaded().run();
    return 0;
}
