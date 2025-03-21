#include <iostream>

#include "baseline.hpp"
#include "global.hpp"
#include "structures.hpp"

int main(int argc, char** argv) {
    // freopen("data/sample_practice.in", "r", stdin);
    std::cin.tie(nullptr);
    std::cout.tie(nullptr);
    std::ios::sync_with_stdio(false);
    // 往 global::init_functions 中添加初始化函数
    global::init_functions.push_back([]() {});
    global::init_functions.push_back(baseline::init_local);
    // 设定写入策略函数
    global::write_strategy_function = baseline::write_strategy;
    // 设定磁头策略函数
    global::head_strategy_function = baseline::head_strategy;
    // 开始运行
    global::run();
    return 0;
}