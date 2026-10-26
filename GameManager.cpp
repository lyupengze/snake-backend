#include "GameManager.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

GameManager::GameManager() {
    init_db();
}

GameManager::~GameManager() {
    if (db_ != nullptr) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

void GameManager::init_db() {
#ifdef _WIN32
    const char* db_path = "snake.db";
#else
    const char* db_path = "/app/data/snake.db";
#endif

    int rc = sqlite3_open(db_path, &db_);
    if (rc != SQLITE_OK) {
        std::cerr << "Unable to open database: " << sqlite3_errmsg(db_) << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(3));
        exit(1);
    }

    sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);

    const char* sql =
        "CREATE TABLE IF NOT EXISTS scores ("
        "name TEXT, mode TEXT, date TEXT, score INTEGER, timestamp INTEGER, "
        "PRIMARY KEY(name, mode, date));";

    int rc_exec = sqlite3_exec(db_, sql, nullptr, nullptr, nullptr);
    if (rc_exec != SQLITE_OK) {
        std::cerr << "Failed to create table: " << sqlite3_errmsg(db_) << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(3));
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

void GameManager::cleanup_expired_sessions() {
    std::time_t now = std::time(nullptr);
    for (auto it = active_sessions_.begin(); it != active_sessions_.end();) {
        if (now - it->second.created_at > GameConfig::kSessionTimeoutSeconds) {
            it = active_sessions_.erase(it);
        } else {
            ++it;
        }
    }
}

std::pair<std::string, std::uint32_t> GameManager::start_game(const std::string& mode) {
    std::lock_guard<std::mutex> lock(session_mtx_);
    cleanup_expired_sessions();

    std::string game_id = generate_game_id();
    std::uint32_t seed = 0;
    std::string date_str = "all";

    std::time_t t = std::time(nullptr);
    std::tm tm_now;
#ifdef _WIN32
    localtime_s(&tm_now, &t);
#else
    localtime_r(&t, &tm_now);
#endif
    char date_buf[11];
    std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &tm_now);

    if (mode == "daily") {
        date_str = date_buf;

        // 用日期 + 密钥盐值派生每日种子，客户端无法逆推。
        // Knuth 乘法哈希保证不可逆性，同时同一天结果确定。
        std::uint32_t base_date =
            (tm_now.tm_year + 1900) * 10000 + (tm_now.tm_mon + 1) * 100 + tm_now.tm_mday;
        const std::uint32_t SECRET_SALT = 0x9E3779B9;
        seed = (base_date ^ SECRET_SALT) * 2654435761u;
    } else {
        std::random_device rd;
        seed = rd();
    }

    active_sessions_[game_id] = {t, seed, mode, date_str, false};
    return {game_id, seed};
}

SubmitResult GameManager::submit_score(const std::string& game_id, const std::string& name,
                                       int score, const std::vector<Action>& actions) {
    if (name.empty() || name.length() > GameConfig::kMaxNameLength) {
        return SubmitResult::InvalidData;
    }

    if (actions.size() > 10000) {
        std::cerr << "Rejected: action count exceeds limit (" << actions.size() << ")" << std::endl;
        return SubmitResult::PayloadTooLarge;
    }

    std::uint32_t real_seed = 0;
    std::string mode = "";
    std::string session_date = "all";
    std::time_t session_created_at = 0;
    {
        std::lock_guard<std::mutex> lock(session_mtx_);
        auto it = active_sessions_.find(game_id);
        if (it == active_sessions_.end() || it->second.submitted) {
            std::cerr << "Rejected: session not found or already submitted (game_id=" << game_id
                      << ")" << std::endl;
            return SubmitResult::Cheating;
        }

        real_seed = it->second.seed;
        mode = it->second.mode;
        session_date = it->second.date;
        session_created_at = it->second.created_at;
        it->second.submitted = true;
    }

    // 校验 tick 序列：必须严格递增且非负。
    int max_tick = 0;
    int last_tick = -1;

    for (const auto& act : actions) {
        if (act.tick < 0) {
            std::cerr << "Rejected: negative tick detected" << std::endl;
            return SubmitResult::Cheating;
        }
        if (act.tick <= last_tick) {
            std::cerr << "Rejected: out-of-order or duplicate tick (current=" << act.tick
                      << ", previous=" << last_tick << ")" << std::endl;
            return SubmitResult::Cheating;
        }
        last_tick = act.tick;

        if (act.tick > max_tick) {
            max_tick = act.tick;
        }
    }

    if (max_tick > 10000) {
        std::cerr << "Rejected: max tick exceeds limit (" << max_tick << ")" << std::endl;
        return SubmitResult::PayloadTooLarge;
    }

    // 若实际耗时远小于理论最短时间，说明使用了加速挂或伪造回放。
    double expected_duration = max_tick * 0.15;
    std::time_t now = std::time(nullptr);
    int actual_duration = static_cast<int>(std::difftime(now, session_created_at));

    if (actual_duration < expected_duration - 3) {
        std::cerr << "Rejected: time paradox — max tick: " << max_tick
                  << ", expected duration: " << expected_duration
                  << "s, actual: " << actual_duration << "s" << std::endl;
        return SubmitResult::Cheating;
    }

    // 核心反作弊：用权威种子在服务端重放游戏，验证客户端声称的分数。
    int calc_score = ReplayEngine::run(real_seed, actions, mode);
    if (calc_score < 0 || calc_score != score) {
        std::cerr << "Rejected: claimed " << score << " pts, replay computed " << calc_score
                  << " pts" << std::endl;
        return SubmitResult::Cheating;
    }

    std::tm tm_now;
#ifdef _WIN32
    localtime_s(&tm_now, &now);
#else
    localtime_r(&now, &tm_now);
#endif

    std::string date_str = session_date;

    // 仅在数据库写入成功后才清除会话，避免 DB 故障导致玩家唯一一次提交机会丢失。
    std::lock_guard<std::mutex> db_lock(db_mtx_);
    const char* sql =
        "INSERT INTO scores (name, mode, date, score, timestamp) VALUES (?, ?, ?, ?, ?) "
        "ON CONFLICT(name, mode, date) DO UPDATE SET score = excluded.score, timestamp = "
        "excluded.timestamp "
        "WHERE excluded.score > scores.score;";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "DB prepare failed: " << sqlite3_errmsg(db_) << std::endl;
        return SubmitResult::InternalError;
    }

    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, mode.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, date_str.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, score);
    sqlite3_bind_int64(stmt, 5, now);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        std::cerr << "DB write failed: " << sqlite3_errmsg(db_) << " (rc=" << rc << ")"
                  << std::endl;
        return SubmitResult::InternalError;
    }

    {
        std::lock_guard<std::mutex> lock(session_mtx_);
        active_sessions_.erase(game_id);
    }
    return SubmitResult::Success;
}

std::vector<ScoreRecord> GameManager::get_top_scores(const std::string& mode, int limit) {
    limit = std::clamp(limit, 1, 100);
    std::lock_guard<std::mutex> db_lock(db_mtx_);
    std::vector<ScoreRecord> top;

    std::time_t t = std::time(nullptr);
    std::tm tm_now;
#ifdef _WIN32
    localtime_s(&tm_now, &t);
#else
    localtime_r(&t, &tm_now);
#endif

    char date_buf[11];
    std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &tm_now);

    // 每日挑战按当天日期筛选；经典模式用 "all" 表示不限日期。
    std::string date_filter = (mode == "daily") ? date_buf : "all";

    const char* sql =
        "SELECT name, score, timestamp FROM scores WHERE mode = ? AND date = ? ORDER BY score DESC "
        "LIMIT ?;";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, mode.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, date_filter.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, limit);

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            ScoreRecord record;
            const unsigned char* text = sqlite3_column_text(stmt, 0);
            record.player_name = text ? reinterpret_cast<const char*>(text) : "Unknown";
            record.score = sqlite3_column_int(stmt, 1);
            record.timestamp = sqlite3_column_int64(stmt, 2);
            record.mode = mode;
            record.date = date_filter;
            top.push_back(record);
        }
    }
    sqlite3_finalize(stmt);

    return top;
}
