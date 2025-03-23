#pragma once

#include <algorithm>
#include <cassert>
#include <vector>

#include "../io.hpp"
#include "utils.hpp"

namespace baseline {

// run 函数自动执行了删除对象时的信息维护
// 写入对象、模拟磁头动作的信息维护任务交给 write_strategy_function 和 head_strategy_function

// ---------------策略----------------

inline std::vector<std::vector<int>> local_disk_slice_p;

inline void init_local() {
    global::disks.resize(global::N + 1, Disk(global::V, global::M));
    local_disk_slice_p.resize(global::N + 1);
    for (int i = 1; i <= global::N; i++) {
        if (i % 2 == 1) {
            local_disk_slice_p[i] = global::disks[i].slice_start;
        } else {
            local_disk_slice_p[i] = global::disks[i].slice_end;
        }
    }
}

inline int move_head(int disk_id, int slice_id, int p, int step) {
    return mod(p, global::disks[disk_id].slice_start[slice_id], global::disks[disk_id].slice_end[slice_id], step);
}

inline std::vector<int> put_forward(int disk_id, int slice_id, int size) {
    std::vector<int> block_id(size + 1);
    const Disk& disk = global::disks[disk_id];
    // 如果 slice 内正好有宽度等于 size 的空余块，则放置在这些块上
    auto it = disk.empty_range.lower_bound(Range{disk.slice_start[slice_id], 0});
    if (it != disk.empty_range.begin()) {
        it--;
    }
    while (it != disk.empty_range.end() && it->l <= disk.slice_end[slice_id]) {
        int l = std::max(disk.slice_start[slice_id], it->l);
        int r = std::min(disk.slice_end[slice_id], it->r);
        if (r - l + 1 == size) {
            for (int i = l; i <= r; i++) {
                assert(disk.blocks[i].object_id == 0);
            }
            for (int i = l; i <= r; i++) {
                block_id[i - l + 1] = i;
            }
            return block_id;
        }
        it++;
    }

    int& p = local_disk_slice_p[disk_id][slice_id];
    for (int i = 1; i <= size; i++) {
        while (disk.blocks[p].object_id != 0) {
            p = move_head(disk_id, slice_id, p, 1);
        }
        block_id[i] = p;
        p = move_head(disk_id, slice_id, p, 1);
    }
    for (int i = 1; i <= size; i++) {
        assert(block_id[i] != 0);
    }
    return block_id;
}

inline std::vector<int> put_back(int disk_id, int slice_id, int size) {
    std::vector<int> block_id(size + 1);
    const Disk& disk = global::disks[disk_id];
    // 如果 slice 内正好有宽度等于 size 的空余块，则放置在这些块上
    auto it = disk.empty_range.lower_bound(Range{disk.slice_end[slice_id], 0});
    if (it == disk.empty_range.end()) {
        it--;
    }
    while (it != disk.empty_range.begin() && it->r >= disk.slice_start[slice_id]) {
        int l = std::max(disk.slice_start[slice_id], it->l);
        int r = std::min(disk.slice_end[slice_id], it->r);
        if (r - l + 1 == size) {
            for (int i = l; i <= r; i++) {
                assert(disk.blocks[i].object_id == 0);
            }
            for (int i = l; i <= r; i++) {
                block_id[i - l + 1] = i;
            }
            return block_id;
        }
        if (it == disk.empty_range.begin()) {
            break;
        }
        it--;
    }

    int& p = local_disk_slice_p[disk_id][slice_id];
    for (int i = 1; i <= size; i++) {
        while (disk.blocks[p].object_id != 0) {
            p = move_head(disk_id, slice_id, p, -1);
        }
        block_id[i] = p;
        p = move_head(disk_id, slice_id, p, -1);
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

    for (size_t i = 0; i < object_index.size(); i++) {
        const ObjectWriteRequest& object = objects[object_index[i]];
        ObjectWriteStrategy& strategy = strategies[object_index[i]];

        strategy.object = object;

        // 选三次硬盘
        for (int i = 0; i < 3; i++) {
            int target_disk_id = 0;
            int target_slice_id = 0;
            // 优先找一个 last_tag 相同且能放下该物体的 slice
            for (int disk_id = 1; disk_id <= global::N; disk_id++) {
                // 不能是已经用了的硬盘
                if (strategy.is_used_disk(disk_id)) {
                    continue;
                }

                Disk& disk = global::disks[disk_id];
                for (int slice_id = 1; slice_id <= disk.slice_num; slice_id++) {
                    if (disk.slice_last_tag[slice_id] == object.tag &&
                        disk.slice_empty_block_num[slice_id] >= object.size) {
                        target_disk_id = disk_id;
                        target_slice_id = slice_id;
                        break;
                    }
                }
                if (target_disk_id != 0) {
                    break;
                }
            }

            // 如果都放不下，则挑一个目前空 slice 最多的硬盘
            if (target_disk_id == 0) {
                int max_empty_slice_num = 0;
                for (int disk_id = 1; disk_id <= global::N; disk_id++) {
                    // 不能是已经用了的硬盘
                    if (strategy.is_used_disk(disk_id)) {
                        continue;
                    }

                    Disk& disk = global::disks[disk_id];
                    int empty_slice_num = 0;
                    for (int slice_id = 1; slice_id <= disk.slice_num; slice_id++) {
                        if (disk.is_slice_empty(slice_id)) {
                            empty_slice_num++;
                        }
                    }
                    if (empty_slice_num > max_empty_slice_num) {
                        max_empty_slice_num = empty_slice_num;
                        target_disk_id = disk_id;
                    }
                }
                if (target_disk_id != 0) {
                    int temp = 0;
                    for (int slice_id = 1; slice_id <= global::disks[target_disk_id].slice_num; slice_id++) {
                        if (global::disks[target_disk_id].is_slice_empty(slice_id) &&
                            global::disks[target_disk_id].slice_empty_block_num[slice_id] >= object.size) {
                            temp = target_disk_id;
                            target_slice_id = slice_id;
                            break;
                        }
                    }
                    // 如果找不到能放的 slice，target 置为 0
                    target_disk_id = temp;
                }
            }

            // 否则，挑一个剩余空间最多且含有该物品 tag 的 slice
            if (target_disk_id == 0) {
                int max_empty_block_num = 0;
                for (int disk_id = 1; disk_id <= global::N; disk_id++) {
                    // 不能是已经用了的硬盘
                    if (strategy.is_used_disk(disk_id)) {
                        continue;
                    }

                    Disk& disk = global::disks[disk_id];
                    for (int slice_id = 1; slice_id <= disk.slice_num; slice_id++) {
                        if (disk.slice_tag[slice_id] & (1 << object.tag) &&
                            disk.slice_empty_block_num[slice_id] > max_empty_block_num &&
                            disk.slice_empty_block_num[slice_id] >= object.size) {
                            max_empty_block_num = disk.slice_empty_block_num[slice_id];
                            target_disk_id = disk_id;
                            target_slice_id = slice_id;
                        }
                    }
                }
            }

            // 否则，挑一个剩余空间最多的 slice
            if (target_disk_id == 0) {
                int max_empty_block_num = 0;
                for (int disk_id = 1; disk_id <= global::N; disk_id++) {
                    // 不能是已经用了的硬盘
                    if (strategy.is_used_disk(disk_id)) {
                        continue;
                    }

                    Disk& disk = global::disks[disk_id];
                    for (int slice_id = 1; slice_id <= disk.slice_num; slice_id++) {
                        if (disk.slice_empty_block_num[slice_id] > max_empty_block_num &&
                            disk.slice_empty_block_num[slice_id] >= object.size) {
                            max_empty_block_num = disk.slice_empty_block_num[slice_id];
                            target_disk_id = disk_id;
                            target_slice_id = slice_id;
                        }
                    }
                }
            }

            if (target_disk_id == 0) {
                // 应该不会出现这种情况
                throw std::runtime_error("No disk can be used.");
            }

            // 选好了硬盘和 slice，开始放置
            strategy.disk_id[i] = target_disk_id;
            if (target_disk_id % 2 == 1) {
                strategy.block_id[i] = put_forward(target_disk_id, target_slice_id, object.size);
            } else {
                strategy.block_id[i] = put_back(target_disk_id, target_slice_id, object.size);
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
        bool vaild_strategy = false;

        while (budget != 0) {
            // 优先使用 read 操作
            const int READ_BUDGET = 150;  // NOTE: 直到有效的 read 操作前可以使用的 token 数
            int pre_size = strategy.actions.size();
            int read_budget = std::min(READ_BUDGET, budget);
            int read_cost = 0;
            int step_read_cost = pre_action != HeadActionType::READ ? 64 : std::max(16, (pre_action_cost * 4 + 4) / 5);
            if (step_read_cost > read_budget) {
                break;
            }
            bool vaild_read = false;
            while (read_budget >= read_cost + step_read_cost) {
                strategy.add_action(HeadActionType::READ);
                pre_action = HeadActionType::READ;
                pre_action_cost = step_read_cost;
                read_cost += step_read_cost;
                if (disk.last_query_time.count(head)) {
                    vaild_read = true;
                    vaild_strategy = true;
                    head = mod(head, 1, global::V, 1);
                    break;
                }
                head = mod(head, 1, global::V, 1);
                // 如果转了一圈，跳出
                if (head == disk.head) {
                    break;
                }
            }
            // 如果转了一圈，跳出
            if (head == disk.head) {
                break;
            }
            if (!vaild_read) {
                // 如果没有有效的 read，清空 read 操作，改成 pass 操作
                strategy.actions.resize(pre_size);
                while(budget > 0) {
                    strategy.add_action(HeadActionType::PASS);
                    budget--;
                    head = mod(head, 1, global::V, 1);
                    pre_action = HeadActionType::PASS;
                    pre_action_cost = 1;
                    if (disk.last_query_time.count(head)) {
                        break;
                    }
                }
                continue;
            }
            // 如果有有效的 read，维护信息，继续下一次循环
            budget -= read_cost;
        }
        // 清空末尾的 pass
        while (!strategy.actions.empty() && strategy.actions.back().type == HeadActionType::PASS) {
            strategy.actions.pop_back();
        }
        // 如果策略中没有有效的 READ，贪心 JUMP 到下一个收益最大的 slice 的可读取开头
        if (!vaild_strategy) {
            strategy.actions.clear();
            // 跳转到收益最大的 slice 的可读取开头
            double max_gain = 0;
            int target_slice = 0;
            for (int slice_id = 1; slice_id <= disk.slice_num; slice_id++) {
                if (disk.slice_margin_gain[slice_id] > max_gain) {
                    max_gain = disk.slice_margin_gain[slice_id];
                    target_slice = slice_id;
                }
            }
            if (target_slice != 0) {
                int target = disk.slice_start[target_slice];
                while (disk.last_query_time.count(target) == 0) {
                    target = mod(target, 1, global::V, 1);
                }
                strategy.add_action(HeadActionType::JUMP, target);
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