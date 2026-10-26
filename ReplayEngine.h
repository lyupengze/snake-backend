#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct Action {
    int tick;
    std::string dir;
};

// 服务端回放引擎：用权威种子 + 客户端操作序列重演整局游戏。
// 返回验证后的得分；若 tick 数超限则返回 -1（防止恶意构造超长回放）。
class ReplayEngine {
public:
    static int run(uint32_t seed, const std::vector<Action>& actions,
                   const std::string& mode = "classic");
};
