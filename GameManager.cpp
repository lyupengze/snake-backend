#include "GameManager.h"

#include <algorithm>
#include <iostream>

GameManager::GameManager() {
    init_db();
}

GameManager::~GameManager() {
    if (db_)
        sqlite3_close(db_);
}

void GameManager::init_db() {
// 【核心】区分 Windows 本地测试和 Linux 容器环境
#ifdef _WIN32
    const char* db_path = "snake.db";
#else
    const char* db_path = "/app/data/snake.db";
#endif

    int rc = sqlite3_open(db_path, &db_);
    if (rc != SQLITE_OK) {
        std::cerr << "无法打开数据库: " << sqlite3_errmsg(db_) << std::endl;
        exit(1);
    }

    // 建表：把 name, mode, date 作为联合主键（如果表已存在，则忽略，不报错）
    const char* sql =
        "CREATE TABLE IF NOT EXISTS scores ("
        "name TEXT, "
        "mode TEXT, "
        "date TEXT, "
        "score INTEGER, "
        "timestamp INTEGER, "
        "PRIMARY KEY(name, mode, date));";

    char* errMsg = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        std::cerr << "建表失败: " << errMsg << std::endl;
        sqlite3_free(errMsg);
        exit(1);
    }
}

std::string GameManager::generate_game_id() {
    thread_local std::mt19937 gen(std::random_device{}());
    std::uniform_int_distribution<> dis(0, 61);
    const char alphanum[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::string id;
    id.reserve(16);
    for (int i = 0; i < 16; ++i)
        id += alphanum[dis(gen)];
    return id;
}

std::string GameManager::start_game() {
    std::string game_id = generate_game_id();
    std::lock_guard<std::mutex> lock(mtx_);
    active_sessions_[game_id] = std::time(nullptr);
    return game_id;
}

bool GameManager::submit_score(const std::string& game_id, const std::string& name, int score,
                               const std::string& mode) {
    std::lock_guard<std::mutex> lock(mtx_);

    auto it = active_sessions_.find(game_id);
    if (it == active_sessions_.end())
        return false;

    std::time_t now = std::time(nullptr);
    int actual_duration = static_cast<int>(std::difftime(now, it->second));
    active_sessions_.erase(it);

    if (score < 0 || score > 500 || actual_duration < 1)
        return false;
    if (static_cast<double>(score) / actual_duration > 50.0)
        return false;

    // 获取今天日期（YYYY-MM-DD）
    std::tm* tm_now = std::localtime(&now);
    char date_buf[11];
    std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", tm_now);
    std::string today_str = date_buf;

    // 如果是经典模式，不区分日期，统一存为 "all"
    std::string date_str = (mode == "daily") ? today_str : "all";

    // 联合主键冲突时，只有新分数大于老分数才更新
    std::string sql = "INSERT INTO scores (name, mode, date, score, timestamp) VALUES ('" + name +
                      "', '" + mode + "', '" + date_str + "', " + std::to_string(score) + ", " +
                      std::to_string(now) +
                      ") "
                      "ON CONFLICT(name, mode, date) DO UPDATE SET score = excluded.score, "
                      "timestamp = excluded.timestamp "
                      "WHERE excluded.score > scores.score;";

    char* errMsg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL 错误: " << errMsg << std::endl;
        sqlite3_free(errMsg);
        return false;
    }

    return true;
}

std::vector<ScoreRecord> GameManager::get_top_scores(const std::string& mode, int limit) {
    std::lock_guard<std::mutex> lock(mtx_);
    std::vector<ScoreRecord> top;

    // 确定日期过滤条件
    std::string date_filter = "all";
    if (mode == "daily") {
        std::time_t t = std::time(nullptr);
        std::tm* tm_now = std::localtime(&t);
        char date_buf[11];
        std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", tm_now);
        date_filter = date_buf;
    }

    // 带上 mode 和 date 进行过滤，保证每日榜单每天零点自动清空
    std::string sql = "SELECT name, score, timestamp FROM scores WHERE mode = '" + mode +
                      "' AND date = '" + date_filter + "' ORDER BY score DESC LIMIT " +
                      std::to_string(limit) + ";";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            ScoreRecord record;
            record.player_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            record.score = sqlite3_column_int(stmt, 1);
            record.timestamp = sqlite3_column_int64(stmt, 2);
            record.mode = mode;  // 顺便记录一下
            record.date = date_filter;
            top.push_back(record);
        }
        sqlite3_finalize(stmt);
    }

    return top;
}