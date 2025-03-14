#include <iostream>

#include "global.hpp"

int main(int argc, char **argv) {
    // 往 global::init_functions 中添加初始化函数
    global::init_functions.push_back([]() {});
    // 设定写入策略函数
    global::write_strategy_function = [](const std::vector<ObjectWriteRequest>& objects) {
        return std::vector<ObjectWriteStrategy>(objects.size());
    };
    // 设定磁头策略函数
    global::head_strategy_function = []() { return std::vector<HeadStrategy>(global::N + 1); };
    // 开始运行
    global::run();
    return 0;
}