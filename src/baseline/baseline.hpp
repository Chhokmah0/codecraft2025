#pragma once

#include <algorithm>
#include <cassert>
#include <vector>

#include "utils.hpp"

namespace baseline {

// run 函数自动执行了删除对象时的信息维护
// 写入对象、模拟磁头动作的信息维护任务交给 write_strategy_function 和 head_strategy_function

// ---------------策略----------------

inline std::vector<int> local_disk_p;

void init_local() { local_disk_p.resize(global::N + 1, 1); }

std::vector<int> put_forward(int disk_id, int size) {
    std::vector<int> block_id(size + 1);
    const Disk& disk = global::disks[disk_id];
    int& p = local_disk_p[disk_id];
    for (int i = 1; i <= size; i++) {
        while (disk.blocks[p].object_id != 0) {
            p = p % global::V + 1;
        }
        block_id[i] = p;
        p = p % global::V + 1;
    }
    for (int i = 1; i <= size; i++) {
        assert(block_id[i] != 0);
    }
    return block_id;
}

std::vector<int> put_back(int disk_id, int size) {
    std::vector<int> block_id(size + 1);
    const Disk& disk = global::disks[disk_id];
    int& p = local_disk_p[disk_id];
    for (int i = 1; i <= size; i++) {
        while (disk.blocks[p].object_id != 0) {
            p = (p - 2 + global::V) % global::V + 1;
        }
        block_id[i] = p;
        p = (p - 2 + global::V) % global::V + 1;
    }
    for (int i = 1; i <= size; i++) {
        assert(block_id[i] != 0);
    }
    return block_id;
}

// 写入策略
inline std::vector<ObjectWriteStrategy> write_strategy_function(const std::vector<ObjectWriteRequest>& objects) {
    std::vector<ObjectWriteStrategy> strategies(objects.size());

    std::vector<int> object_index(objects.size());
    std::iota(object_index.begin(), object_index.end(), 0);
    // 将具有相同 tag 的元素放在一起
    // REVIEW: 并优先放大的
    std::sort(object_index.begin(), object_index.end(), [&](int i, int j) {
        if (objects[i].tag == objects[j].tag) {
            return objects[i].size > objects[j].size;
        }
        return objects[i].tag < objects[j].tag;
    });

    for (int i = 0; i < objects.size(); i++) {
        const ObjectWriteRequest& object = objects[object_index[i]];
        ObjectWriteStrategy& strategy = strategies[object_index[i]];

        strategy.object = object;

        // 选定硬盘
        int target_disk_id = ((object.tag - 1) / 3 * 3) % global::N + 1;
        for (int j = 0; j < 3; j++) {
            while (global::disks[target_disk_id].empty_block_num < object.size) {
                target_disk_id = target_disk_id % global::N + 1;
            }
            strategy.disk_id[j] = target_disk_id;
            target_disk_id = target_disk_id % global::N + 1;

            // 选定块，根据硬盘的奇偶性质确定放置方向
            if (strategy.disk_id[j] % 2 == 1) {
                strategy.block_id[j] = put_forward(strategy.disk_id[j], object.size);
            } else {
                strategy.block_id[j] = put_back(strategy.disk_id[j], object.size);
            }
        }

        // 保证第二个硬盘上的顺序和第一个硬盘上不一样（put_back 本身就会反向放置）
        if (strategy.disk_id[1] % 2 == strategy.disk_id[0] % 2) {
            std::reverse(strategy.block_id[1].begin() + 1, strategy.block_id[1].end());
        }

        // 随机打乱第三个硬盘上的顺序
        // tmp[i]是第i个盘放哪个对象块
        std::shuffle(strategy.block_id[2].begin() + 1, strategy.block_id[2].end(), global::rng);

        write_object(strategy);
    }

    return strategies;
}

// 磁头移动策略
inline std::vector<HeadStrategy> head_strategy_function() {
    std::vector<HeadStrategy> head_strategies(global::N + 1);
    std::vector<int> index(global::N + 1);
    std::iota(index.begin(), index.end(), 0);
    // 优先模拟收益较小的磁盘，此时收益较大的磁盘仍然具有收益，因此可以保证负载均衡
    std::sort(index.begin() + 1, index.end(),
              [&](int i, int j) { return global::disks[i].total_margin_gain < global::disks[j].total_margin_gain; });
    for (int i = 1; i <= global::N; i++) {
        Disk& disk = global::disks[index[i]];
        HeadStrategy& strategy = head_strategies[index[i]];
        if (disk.last_query_time.empty()) {
            continue;
        }
        // 考虑某个磁头的策略
        int head = disk.head;
        HeadActionType pre_action = disk.pre_action;
        int pre_action_cost = disk.pre_action_cost;
        int budget = global::G;

        while (budget != 0) {
            if (disk.margin_gain[head] > 0) {
                auto cost = pre_action != HeadActionType::READ ? 64 : std::max(16, (pre_action_cost * 4 + 4) / 5);
                if (budget >= cost) {
                    strategy.add_action(HeadActionType::READ);
                    budget -= cost;
                    pre_action = HeadActionType::READ;
                    pre_action_cost = cost;
                } else {
                    break;
                }
            } else {
                strategy.add_action(HeadActionType::PASS);
                budget -= 1;
                pre_action = HeadActionType::PASS;
                pre_action_cost = 1;
            }
            head = (head) % global::V + 1;
            // 如果转了一圈，跳出
            if (head == disk.head) {
                break;
            }
        }
        // 清空末尾的 pass
        while (!strategy.actions.empty() && strategy.actions.back().type == HeadActionType::PASS) {
            strategy.actions.pop_back();
        }
        // 如果策略中没有 READ，JUMP 到下一个需要读取的块
        if (std::all_of(strategy.actions.begin(), strategy.actions.end(),
                        [](const HeadAction& action) { return action.type != HeadActionType::READ; })) {
            strategy.actions.clear();
            // 跳转到下一个需要读取的块
            for (int head = (disk.head + 1) % global::V; head != disk.head; head = (head + 1) % global::V) {
                if (disk.margin_gain[head] > 0) {
                    strategy.add_action(HeadActionType::JUMP, head);
                    break;
                }
            }
        }

        // 模拟磁头动作
        simulate_head(disk, strategy);
    }
    return head_strategies;
}

// ---------------交互----------------
// 应该是不需要修改
inline void run() {
    io::init_input();
    init_local();
    io::init_output();

    for (global::timestamp = 1; global::timestamp <= global::T + 105; global::timestamp++) {
        // 时间片交互事件
        io::timestamp_align(global::timestamp);

        // 对象删除事件
        auto deleted_objects = io::delete_object_input();
        deleted_requests.clear();
        for (int object_id : deleted_objects) {
            delete_object(object_id);
        }
        io::delete_object_output(deleted_requests);

        // 对象写入事件
        auto write_objects = io::write_object_input();
        // NOTE: 模拟写入的任务交给 write_strategy_function
        auto write_strategies = write_strategy_function(write_objects);
        io::write_object_output(write_strategies);

        // 对象读取事件
        auto read_objects = io::read_object_input();
        for (const auto& [req_id, object_id] : read_objects) {
            Object& object = global::objects[object_id];
            object.add_request(req_id);
            for (int i = 0; i < 3; i++) {
                Disk& disk = global::disks[object.disk_id[i]];
                for (int j = 1; j <= object.size; j++) {
                    disk.query(object.block_id[i][j], global::timestamp);
                }
            }
        }

        // NOTE: 模拟磁盘头动作的任务交给 head_strategy_function
        completed_requests.clear();
        auto head_strategies = head_strategy_function();
        io::read_object_output(head_strategies, completed_requests);
    }
}
}  // namespace baseline