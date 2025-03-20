#pragma once
#include <algorithm>
#include <cassert>
#include <functional>
#include <iostream>
#include <numeric>
#include <random>
#include <unordered_map>
#include <vector>

#include "structures.hpp"

namespace global {

int T;  // 有 T 个时间片存在读取、写入、删除操作，本次数据有 T+105 个时间片
int M;  // 对象标签的数量
int N;  // 存储系统中硬盘的个数
int V;  // 每个硬盘中存储单元的个树
int G;  // 每个磁头每个时间片最多消耗的令牌数

// 输入写入的对象，返回写入策略
std::function<std::vector<ObjectWriteStrategy>(const std::vector<ObjectWriteRequest>&)> write_strategy_function =
    [](const std::vector<ObjectWriteRequest>& objects) { return std::vector<ObjectWriteStrategy>(objects.size()); };
// 返回磁头策略
std::function<HeadStrategy(int)> head_strategy_function = [](int) { return HeadStrategy{}; };
// 初始化函数，在初始化时会被调用
std::vector<std::function<void()>> init_functions;

std::mt19937 rng;

// fre_xxx[i][j] 表示相应时间片内对象标签为 i 的读取、写入、删除操作的对象大小之和。
// i 和 j 从 1 开始编号
std::vector<std::vector<int>> fre_del, fre_write, fre_read;
int fre_len;

int timestamp;                            // 全局时间戳
std::vector<Disk> disks;                  // 从 1 开始编号
std::unordered_map<int, Object> objects;  // (object_id, Object)

std::vector<int> deleted_requests;    // 已经删除的请求
std::vector<int> completed_requests;  // 已经完成的请求

// 获取第 disk_id 硬盘上的第 block_index 个块的请求数量（从 1 开始编号）
int get_request_number(int disk_id, int block_index) {
    ObjectBlock object = disks[disk_id].blocks[block_index];
    if (object.object_id == 0) {
        return 0;
    }
    return objects[object.object_id].get_request_number(object.block_index);
}

// 获取硬盘上的第 block_index 个块的请求数量（从 1 开始编号）
int get_request_number(const Disk& disk, int block_index) {
    ObjectBlock object = disk.blocks[block_index];
    if (object.object_id == 0) {
        return 0;
    }
    return objects[object.object_id].get_request_number(object.block_index);
}

void delete_object(int object_id) {
    assert(objects.count(object_id));

    // 清理硬盘上的数据
    Object& object = objects[object_id];
    for (int i = 0; i < 3; i++) {
        Disk& disk = disks[object.disk_id[i]];
        for (int j = 1; j <= object.size; j++) {
            assert(disk.blocks[object.block_id[i][j]].object_id == object_id);

            disk.blocks[object.block_id[i][j]].object_id = 0;
            disk.unuse(object.block_id[i][j], object.tag);
            if (disk.query.count(object.block_id[i][j])) {
                disk.query.erase(object.block_id[i][j]);
            }
        }
        disk.empty_block_num += object.size;
    }

    // 清理对象，删除所有请求
    for (const auto& [req_id, request] : object.get_read_requests()) {
        deleted_requests.push_back(req_id);
    }
    objects.erase(object_id);
}

void simulate_head(Disk& disk, const HeadStrategy& strategy) {
    for (const auto& action : strategy.actions) {
        switch (action.type) {
            case HeadActionType::JUMP: {
                disk.pre_action = HeadActionType::JUMP;
                disk.pre_action_cost = G;
                disk.head = action.target;
                break;
            }
            case HeadActionType::READ: {
                auto cost =
                    disk.pre_action != HeadActionType::READ ? 64 : std::max(16, (disk.pre_action_cost * 4 + 4) / 5);
                disk.pre_action = HeadActionType::READ;
                disk.pre_action_cost = cost;

                ObjectBlock& block = disk.blocks[disk.head];
                assert(block.object_id != 0);
                Object& object = objects[block.object_id];
                for (int i = 0; i < 3; i++) {
                    Disk& t_disk = disks[object.disk_id[i]];
                    if (t_disk.query.count(object.block_id[i][block.block_index])) {
                        t_disk.query.erase(object.block_id[i][block.block_index]);
                    }
                }

                // 在读取操作时，更新 disk 中的请求数量
                auto temp_completed_requests = objects[block.object_id].read(block.block_index);
                completed_requests.insert(completed_requests.end(), temp_completed_requests.begin(),
                                          temp_completed_requests.end());
                disk.head = disk.head % V + 1;
                break;
            }
            case HeadActionType::PASS: {
                disk.pre_action = HeadActionType::PASS;
                disk.pre_action_cost = 1;
                disk.head = disk.head % V + 1;
                break;
            }
        }
    }
}

void run() {
    std::cin >> T >> M >> N >> V >> G;
    objects.reserve(100000);

    disks.resize(N + 1, Disk(V));

    // 初始化用的信息
    fre_len = (T + 1799) / 1800;
    for (int i = 1; i <= M; i++) {
        fre_del.resize(M + 1);
        for (int j = 1; j <= fre_len; j++) {
            fre_del[i].resize(fre_len + 1);
            std::cin >> fre_del[i][j];
        }
    }
    for (int i = 1; i <= M; i++) {
        fre_write.resize(M + 1);
        for (int j = 1; j <= fre_len; j++) {
            fre_write[i].resize(fre_len + 1);
            std::cin >> fre_write[i][j];
        }
    }
    for (int i = 1; i <= M; i++) {
        fre_read.resize(M + 1);
        for (int j = 1; j <= fre_len; j++) {
            fre_read[i].resize(fre_len + 1);
            std::cin >> fre_read[i][j];
        }
    }
    // 初始化
    for (auto& f : init_functions) {
        f();
    }

    // 初始化完毕
    std::cout << "OK" << '\n';
    std::cout.flush();

    // 开始模拟
    for (timestamp = 1; timestamp <= T + 105; timestamp++) {
        // 时间片对齐事件
        std::string event;
        int time;
        std::cin >> event >> time;
        std::cout << event << " " << timestamp << '\n';
        std::cout.flush();
        assert(time == timestamp);

        // 对象删除事件
        int n_delete;
        std::cin >> n_delete;
        for (int i = 0; i < n_delete; i++) {
            int object_id;
            std::cin >> object_id;
            delete_object(object_id);
        }
        std::cout << deleted_requests.size() << '\n';
        for (auto req_id : deleted_requests) {
            std::cout << req_id << '\n';
        }
        deleted_requests.clear();
        std::cout.flush();

        // 对象写入事件
        int n_write;
        std::cin >> n_write;
        std::vector<ObjectWriteRequest> write_objects(n_write);
        for (int i = 0; i < n_write; i++) {
            std::cin >> write_objects[i].id >> write_objects[i].size >> write_objects[i].tag;
        }
        std::vector<ObjectWriteStrategy> write_strategies = write_strategy_function(write_objects);
        // 输出策略
        for (auto& strategy : write_strategies) {
            std::cout << strategy.object.id << '\n';
            for (int i = 0; i < 3; i++) {
                std::cout << strategy.disk_id[i] << " ";
                for (int j = 1; j <= strategy.object.size; j++) {
                    std::cout << strategy.block_id[i][j] << " \n"[j == strategy.object.size];
                }
            }
        }
        std::cout.flush();
        // 检查策略是否合法，根据策略更新状态
        for (auto& strategy : write_strategies) {
            for (int i = 0; i < 3; i++) {
                Disk& disk = disks[strategy.disk_id[i]];
                for (int j = 1; j <= strategy.object.size; j++) {
                    assert(1 <= strategy.block_id[i][j] && strategy.block_id[i][j] <= V);
                    assert(disk.blocks[strategy.block_id[i][j]].object_id == strategy.object.id);
                    disk.blocks[strategy.block_id[i][j]].object_id = strategy.object.id;
                    disk.blocks[strategy.block_id[i][j]].block_index = j;
                    // disk.use(strategy.block_id[i][j], strategy.object.tag);
                }
                disk.empty_block_num -= strategy.object.size;
            }

            objects[strategy.object.id] = Object(strategy);
        }

        // 对象读取事件
        int n_read;
        std::cin >> n_read;
        for (int i = 0; i < n_read; i++) {
            int req_id, obj_id;
            std::cin >> req_id >> obj_id;
            Object& object = objects[obj_id];
            object.add_request(req_id);
            // 更新到磁盘上
            for (int j = 0; j < 3; j++) {
                Disk& disk = disks[object.disk_id[j]];
                for (int k = 1; k <= object.size; k++) {
                    disk.query[object.block_id[j][k]] = timestamp;
                }
            }
        }
        // 尝试随机化
        std::vector<HeadStrategy> head_strategies(N + 1);
        std::vector<int> index(N + 1);
        std::iota(index.begin(), index.end(), 0);
        std::shuffle(index.begin() + 1, index.end(), rng);
        // 模拟磁头动作
        for (int i = 1; i <= N; i++) {
            // 获取读取策略
            head_strategies[index[i]] = head_strategy_function(index[i]);
            simulate_head(disks[index[i]], head_strategies[index[i]]);
        }
        for (int i = 1; i <= N; i++) {
            std::cout << head_strategies[i] << '\n';
        }
        // 输出完成的请求
        std::cout << completed_requests.size() << '\n';
        for (auto req_id : completed_requests) {
            std::cout << req_id << '\n';
        }
        completed_requests.clear();
        std::cout.flush();

        // check
        for (int i = 1; i <= N; i++) {
            Disk& disk = disks[i];
            if (disk.used.empty()) {
                assert(disk.empty_range.size() == 1);
                assert(disk.empty_range.begin()->l == 1);
                assert(disk.empty_range.begin()->r == V);
            }
        }
    }
}

}  // namespace global