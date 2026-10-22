#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct Action {
    int tick;
    std::string dir;
};

class ReplayEngine {
public:
    // 静态方法：输入种子和操作，返回服务器计算出的真实分数
    static int run(uint32_t seed, const std::vector<Action>& actions);
};