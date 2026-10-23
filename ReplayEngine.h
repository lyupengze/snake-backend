#pragma once
#include <cstdint>
#include <string>
#include <vector>

/**
 * 玩家操作记录，用于后端重放验证
 */
struct Action {
    int tick;         // 该操作发生的游戏帧数 (从 0 开始)
    std::string dir;  // 移动方向 ("UP", "DOWN", "LEFT", "RIGHT")
};

/**
 * 服务器权威重放引擎
 *
 * 负责根据前端提交的操作序列，在服务器端完整模拟游戏过程，以此验证玩家分数的真实性。
 */
class ReplayEngine {
public:
    /**
     * 执行游戏重放
     *
     * @param seed 开局时服务器下发的权威随机种子
     * @param actions 玩家提交的操作序列
     * @return int 服务器计算出的真实分数；若检测到异常（如超长重放攻击）返回 -1
     */
    static int run(uint32_t seed, const std::vector<Action>& actions);
};