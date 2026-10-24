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

/**
 * 初始化 SQLite 数据库连接与表结构
 */
void GameManager::init_db() {
#ifdef _WIN32
    const char* db_path = "snake.db";
#else
    const char* db_path = "/app/data/snake.db";
#endif

    int rc = sqlite3_open(db_path, &db_);
    if (rc != SQLITE_OK) {
        std::cerr << "无法打开数据库: " << sqlite3_errmsg(db_) << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(3));
        exit(1);
    }

    // 开启 WAL 模式，提高并发读写性能
    sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);

    const char* sql =
        "CREATE TABLE IF NOT EXISTS scores ("
        "name TEXT, mode TEXT, date TEXT, score INTEGER, timestamp INTEGER, "
        "PRIMARY KEY(name, mode, date));";

    int rc_exec = sqlite3_exec(db_, sql, nullptr, nullptr, nullptr);
    if (rc_exec != SQLITE_OK) {
        std::cerr << "建表失败: " << sqlite3_errmsg(db_) << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(3));
        exit(1);
    }
}

/**
 * 生成 16 位随机字符串作为唯一游戏会话 ID
 */
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

/**
 * 清理超时的未提交游戏会话（防止内存泄漏）
 */
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

/**
 * 玩家请求开局：分配 gameId 和权威 seed
 */
std::pair<std::string, std::uint32_t> GameManager::start_game(const std::string& mode) {
    std::lock_guard<std::mutex> lock(session_mtx_);
    cleanup_expired_sessions();

    std::string game_id = generate_game_id();
    std::uint32_t seed = 0;
    std::string date_str = "all";  // 默认经典模式

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
        // 每日挑战：使用日期（如 20231024）作为种子，并记录日期
        date_str = date_buf;
        seed = (tm_now.tm_year + 1900) * 10000 + (tm_now.tm_mon + 1) * 100 + tm_now.tm_mday;
    } else {
        // 经典模式：真随机种子
        std::random_device rd;
        seed = rd();
    }

    active_sessions_[game_id] = {t, seed, mode, date_str, false};
    return {game_id, seed};
}

/**
 * 核心业务：处理分数提交与防作弊校验（服务器权威重放）
 */
SubmitResult GameManager::submit_score(const std::string& game_id, const std::string& name,
                                       int score, const std::vector<Action>& actions) {
    // [校验 1] 玩家名称合法性
    if (name.empty() || name.length() > GameConfig::kMaxNameLength) {
        return SubmitResult::InvalidData;
    }

    // [校验 2] 防 DoS 攻击：限制单局最大操作记录数
    if (actions.size() > 10000) {
        std::cerr << "拦截: actions 数量超限 (" << actions.size() << ")" << std::endl;
        return SubmitResult::PayloadTooLarge;
    }

    std::uint32_t real_seed = 0;
    std::string mode = "";
    std::string session_date = "all";
    std::time_t session_created_at = 0;
    {
        // [校验 3] 验证 Session 有效性
        std::lock_guard<std::mutex> lock(session_mtx_);
        auto it = active_sessions_.find(game_id);
        if (it == active_sessions_.end() || it->second.submitted) {
            std::cerr << "拦截: session 不存在或已提交 (game_id=" << game_id << ")" << std::endl;
            return SubmitResult::Cheating;
        }

        real_seed = it->second.seed;
        mode = it->second.mode;
        session_date = it->second.date;
        session_created_at = it->second.created_at;
        it->second.submitted = true;
    }

    // [校验 4] 遍历校验操作时序，寻找最大 tick
    int max_tick = 0;
    int last_tick = -1;

    for (const auto& act : actions) {
        if (act.tick < 0) {
            std::cerr << "作弊拦截: 检测到负数 tick" << std::endl;
            return SubmitResult::Cheating;
        }
        if (act.tick <= last_tick) {
            std::cerr << "作弊拦截: 检测到乱序或重复 tick (当前=" << act.tick
                      << ", 上一个=" << last_tick << ")" << std::endl;
            return SubmitResult::Cheating;
        }
        last_tick = act.tick;

        if (act.tick > max_tick) {
            max_tick = act.tick;
        }
    }

    // [校验 5] 防极限构造 DoS：限制最大帧数
    if (max_tick > 10000) {
        std::cerr << "拦截: 超过最大帧数限制 (" << max_tick << ")" << std::endl;
        return SubmitResult::PayloadTooLarge;
    }

    // [校验 6] 时间悖论检测
    double expected_duration = max_tick * 0.15;
    std::time_t now = std::time(nullptr);
    int actual_duration = static_cast<int>(std::difftime(now, session_created_at));

    if (actual_duration < expected_duration - 3) {
        std::cerr << "作弊拦截: 时间悖论! 声称最大 tick: " << max_tick
                  << "，要求物理耗时: " << expected_duration << "s，实际耗时: " << actual_duration
                  << "s" << std::endl;
        return SubmitResult::Cheating;
    }

    // [校验 7] 核心防御：服务器使用 real_seed 重放玩家操作，计算真实分数
    // [修改] 将 mode 参数传递进去
    int calc_score = ReplayEngine::run(real_seed, actions, mode);
    if (calc_score < 0 || calc_score != score) {
        // ... 后续逻辑不变
        std::cerr << "作弊拦截: 声称 " << score << " 分, 实际重放 " << calc_score << " 分"
                  << std::endl;
        return SubmitResult::Cheating;
    }

    std::tm tm_now;
#ifdef _WIN32
    localtime_s(&tm_now, &now);
#else
    localtime_r(&now, &tm_now);
#endif

    std::string date_str = session_date;

    // [写入数据库]
    std::lock_guard<std::mutex> db_lock(db_mtx_);
    const char* sql =
        "INSERT INTO scores (name, mode, date, score, timestamp) VALUES (?, ?, ?, ?, ?) "
        "ON CONFLICT(name, mode, date) DO UPDATE SET score = excluded.score, timestamp = "
        "excluded.timestamp "
        "WHERE excluded.score > scores.score;";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "数据库准备失败: " << sqlite3_errmsg(db_) << std::endl;
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
        std::cerr << "数据库写入失败: " << sqlite3_errmsg(db_) << " (rc=" << rc << ")" << std::endl;
        return SubmitResult::InternalError;
    }

    // [安全修复 P3] 只有数据库写入成功后，才销毁
    // Session，避免因写库失败导致玩家白白丢失一次提交机会
    {
        std::lock_guard<std::mutex> lock(session_mtx_);
        active_sessions_.erase(game_id);
    }
    return SubmitResult::Success;
}

/**
 * 获取排行榜数据
 */
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

    // 每日挑战只看当天的记录，经典模式看 "all"
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