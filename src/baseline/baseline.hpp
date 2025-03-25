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
inline std::vector<int> should_jmp;  // 跳出一个 slice 时，强制跳转

inline void init_local() {
    should_jmp.resize(global::N + 1, 0);
    global::disks.resize(global::N + 1, Disk(global::V, global::M));
    local_disk_slice_p.resize(global::N + 1);
    for (int i = 1; i <= global::N; i++) {
        /* if (i % 2 == 1) {
             local_disk_slice_p[i] = global::disks[i].slice_start;
         } else {
             local_disk_slice_p[i] = global::disks[i].slice_end;
         }*/
        local_disk_slice_p[i] = global::disks[i].slice_start;
    }
}

inline int move_head(int disk_id, int slice_id, int p, int step) {
    return mod(p, global::disks[disk_id].slice_start[slice_id], global::disks[disk_id].slice_end[slice_id], step);
}

inline std::vector<int> put_forward(int disk_id, int slice_id, int size) {
    std::vector<int> block_id(size + 1);
    const Disk& disk = global::disks[disk_id];
    // 选择策略：选择最短的能放下size个块的空间
    int fir = disk.slice_start[slice_id];
    int end_pos = disk.slice_end[slice_id];
    int lst = fir - 1;
    int count = 0;
    int min_len_p = -1, min_len = 2e9;
    while (lst < end_pos && count < size) {
        lst++;
        if (disk.blocks[lst].object_id == 0) count++;
    }
    for (int i = fir;; i++) {
        if (lst == end_pos && count < size) break;
        if (lst - i + 1 < min_len) {
            min_len = lst - i + 1;
            min_len_p = i;
        }
        if (disk.blocks[i].object_id == 0) count--;
        while (lst < end_pos && count < size) {
            lst++;
            if (disk.blocks[lst].object_id == 0) count++;
        }
    }
    int p = min_len_p;
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

// 首先去所有 slices 里找拥有当前 tag 且 slice 里 tag 数量最少的，数量相同取剩余空间最大的，仍然相同就随机选一个
inline int find_disk1(const std::vector<std::pair<int, int>>& disk_block, int size, int tag) {
    int now = -1, now_status = 2147483647, res = -1, lst_size = -1;
    for (auto [disk_id, slice_id] : disk_block) {
        Disk& disk = global::disks[disk_id];
        int nxt_status = disk.slice_tag[slice_id];
        if (disk.slice_empty_block_num[slice_id] >= size) {
            if (__builtin_popcount(nxt_status) < __builtin_popcount(now_status) &&
                (nxt_status & (1 << tag)) == (1 << tag)) {
                now = disk_id;
                now_status = nxt_status;
                lst_size = disk.slice_empty_block_num[slice_id];
            } else if (__builtin_popcount(nxt_status) == __builtin_popcount(now_status) &&
                       (nxt_status & (1 << tag)) == (1 << tag) && disk.slice_empty_block_num[slice_id] > lst_size) {
                now = disk_id;
                now_status = nxt_status;
                lst_size = disk.slice_empty_block_num[slice_id];
            }
        }
    }
    return now;
}
// 在这个硬盘里找 tag 数量最少的 slice，如果相同找剩余空间最大的，仍然相同就随机
inline int find_disk2(const std::vector<std::pair<int, int>>& disk_block, int size, int tag) {
    int now = -1, now_status = 2147483647, res = -1, lst_size = -1;
    for (auto [disk_id, slice_id] : disk_block) {
        Disk& disk = global::disks[disk_id];
        int nxt_status = disk.slice_tag[slice_id];
        if (disk.slice_empty_block_num[slice_id] >= size) {
            if (__builtin_popcount(nxt_status) < __builtin_popcount(now_status)) {
                now = disk_id;
                now_status = nxt_status;
                lst_size = disk.slice_empty_block_num[slice_id];
            } else if (__builtin_popcount(nxt_status) == __builtin_popcount(now_status) &&
                       disk.slice_empty_block_num[slice_id] > lst_size) {
                now = disk_id;
                now_status = nxt_status;
                lst_size = disk.slice_empty_block_num[slice_id];
            }
        }
    }
    return now;
}

inline int find_slice(int disk_id, int size, int tag) {
    const Disk& disk = global::disks[disk_id];
    int now = -1, now_status = 2147483647, res = -1, lst_size = -1;
    for (int slice_id = 1; slice_id <= disk.slice_num; slice_id++) {
        int nxt_status = disk.slice_tag[slice_id];
        if (disk.slice_empty_block_num[slice_id] >= size) {
            if (__builtin_popcount(nxt_status) < __builtin_popcount(now_status) &&
                (nxt_status & (1 << tag)) == (1 << tag))
                now = slice_id, now_status = nxt_status, res = slice_id,
                lst_size = disk.slice_empty_block_num[slice_id];
            else if (__builtin_popcount(nxt_status) == __builtin_popcount(now_status) &&
                     (nxt_status & (1 << tag)) == (1 << tag) && disk.slice_empty_block_num[slice_id] > lst_size)
                now = slice_id, now_status = nxt_status, res = slice_id,
                lst_size = disk.slice_empty_block_num[slice_id];
        }
    }
    if (res != -1) return res;
    for (int slice_id = 1; slice_id <= disk.slice_num; slice_id++) {
        int nxt_status = disk.slice_tag[slice_id];
        if (disk.slice_empty_block_num[slice_id] >= size) {
            if (__builtin_popcount(nxt_status) < __builtin_popcount(now_status))
                now = slice_id, now_status = nxt_status, res = slice_id,
                lst_size = disk.slice_empty_block_num[slice_id];
            else if (__builtin_popcount(nxt_status) == __builtin_popcount(now_status) &&
                     disk.slice_empty_block_num[slice_id] > lst_size)
                now = slice_id, now_status = nxt_status, res = slice_id,
                lst_size = disk.slice_empty_block_num[slice_id];
        }
    }
    return res;
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
        std::vector<int> disk_ids(global::N);
        std::iota(disk_ids.begin(), disk_ids.end(), 1);
        // 将 (tag - 1) % V + 1 作为优先的硬盘，同时也优先考虑这里往后的硬盘
        // std::rotate(disk_ids.begin(), disk_ids.begin() + (object.tag - 1) % global::N, disk_ids.end());
        // 将已经存在 "slice 的 last_tag" 和 "object 的 tag" 相同的硬盘延后考虑
        // 保证一定的负载均衡
        // auto not_have_slice_same_tag = [&](int disk_id) {
        //     for (int slice_id = 1; slice_id <= global::disks[disk_id].slice_num; slice_id++) {
        //         if (global::disks[disk_id].slice_last_tag[slice_id] == object.tag) {
        //             return false;
        //         }
        //     }
        //     return true;
        // };
        // std::stable_partition(disk_ids.begin(), disk_ids.end(), not_have_slice_same_tag);
        // std::shuffle(disk_ids.begin(), disk_ids.end(), global::rng);
        // 选三次硬盘
        for (int i = 0; i < 3; i++) {
            std::vector<std::pair<int, int>> disk_block;
            int target_disk_id = -1;
            // int max_empty_block_num = 0;
            for (auto disk_id : disk_ids) {
                // 不能是已经用了的硬盘
                if (strategy.is_used_disk(disk_id)) {
                    continue;
                }
                Disk& disk = global::disks[disk_id];
                for (int slice_id = 1; slice_id <= disk.slice_num; slice_id++) {
                    disk_block.push_back({disk_id, slice_id});
                }
            }
            target_disk_id = find_disk1(disk_block, object.size, object.tag);

            if (target_disk_id == -1) {  // 第一步没找到有这个tag的，去选空的slices最多的硬盘，空的数量相同随机选一个盘
                int max_block_num = 0;
                std::vector<int> disk_list;
                for (auto disk_id : disk_ids) {
                    // 不能是已经用了的硬盘
                    if (strategy.is_used_disk(disk_id)) {
                        continue;
                    }

                    Disk& disk = global::disks[disk_id];
                    int empty_block_num = 0;
                    for (int slice_id = 1; slice_id <= disk.slice_num; slice_id++) {
                        int status = disk.slice_tag[slice_id];
                        if (status == 0) empty_block_num++;
                        // if (disk.slice_empty_block_num[slice_id] >= object.size) {
                        //     empty_block_num += disk.slice_empty_block_num[slice_id];
                        // }待尝试的方向
                    }
                    if (empty_block_num > max_block_num) {
                        max_block_num = empty_block_num;
                        disk_list.clear();
                    }
                    if (empty_block_num == max_block_num) {
                        disk_list.push_back(disk_id);
                    }
                }
                // 优先空的块尽可能多的盘
                std::shuffle(disk_list.begin(), disk_list.end(), global::rng);
                if (max_block_num > 0) {
                    target_disk_id = disk_list[(long long)global::rng() % disk_list.size()];
                } else {  // 没有为空的硬盘，选tag最少的slice
                    target_disk_id = find_disk2(disk_block, object.size, object.tag);
                }
            }

            if (target_disk_id == -1) {
                // 应该不会出现这种情况
                throw std::runtime_error("No disk can be used.");
            }
            // 选好了硬盘和 slice，开始放置
            strategy.disk_id[i] = target_disk_id;
            strategy.slice_id[i] = find_slice(strategy.disk_id[i], object.size, object.tag);
            // if (target_disk_id % 2 == 1) {
            strategy.block_id[i] = put_forward(target_disk_id, strategy.slice_id[i], object.size);
            // } else {
            //     strategy.block_id[i] = put_back(target_disk_id, target_slice_id, object.size);
            // }
        }

        // 保证第二个硬盘上的顺序和第一个硬盘上不一样（put_back 本身就会反向放置）
        // if (strategy.disk_id[1] % 2 == strategy.disk_id[0] % 2) {
        //     std::reverse(strategy.block_id[1].begin() + 1, strategy.block_id[1].end());
        // }

        // 随机打乱第三个硬盘上的顺序
        // tmp[i]是第i个盘放哪个对象块
        // std::shuffle(strategy.block_id[2].begin() + 1, strategy.block_id[2].end(), global::rng);

        write_object(strategy);
    }

    return strategies;
}

// 磁头移动策略
inline HeadStrategy single_head_strategy_function(int disk_id) {
    Disk& disk = global::disks[disk_id];
    HeadStrategy strategy;
    if (disk.last_query_time.empty()) {
        return strategy;
    }
    // 考虑某个磁头的策略
    int head = disk.head;
    HeadActionType pre_action = disk.pre_action;
    int pre_action_cost = disk.pre_action_cost;
    int budget = global::G;
    bool valid_strategy = false;

    while (budget != 0) {
        // 优先使用 read 操作
        const int READ_BUDGET = 150;  // NOTE: 直到有效的 read 操作前可以使用的 token 数
        int pre_size = strategy.actions.size();
        int pre_head = head;
        int read_budget = std::min(READ_BUDGET, budget);
        int read_cost = 0;
        int step_read_cost = pre_action != HeadActionType::READ ? 64 : std::max(16, (pre_action_cost * 4 + 4) / 5);
        if (step_read_cost > read_budget) {
            break;
        }
        bool valid_read = false;
        while (read_budget >= read_cost + step_read_cost) {
            strategy.add_action(HeadActionType::READ);
            pre_action = HeadActionType::READ;
            pre_action_cost = step_read_cost;
            read_cost += step_read_cost;
            if (disk.query_num[head]) {
                valid_read = true;
                valid_strategy = true;
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
        if (!valid_read) {
            // 如果没有有效的 read，清空 read 操作，改成 pass 操作
            strategy.actions.resize(pre_size);
            head = pre_head;
            while (budget > 0) {
                strategy.add_action(HeadActionType::PASS);
                budget--;
                head = mod(head, 1, global::V, 1);
                pre_action = HeadActionType::PASS;
                pre_action_cost = 1;
                if (disk.query_num[head]) {
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
    if (!valid_strategy || should_jmp[disk_id]) {
        strategy.actions.clear();
        // 跳转到收益最大的 slice 的可读取开头
        double max_gain = 0;
        int target_slice = 0;
        for (int slice_id = 1; slice_id <= disk.slice_num; slice_id++) {
            if (disk.slice_gain(slice_id) > max_gain) {
                max_gain = disk.slice_margin_gain[slice_id];
                target_slice = slice_id;
            }
        }
        if (target_slice != 0) {
            int target = disk.slice_start[target_slice];
            while (disk.query_num[target] == 0) {
                target = mod(target, 1, global::V, 1);
            }
            strategy.add_action(HeadActionType::JUMP, target);
        }
    }
    return strategy;
}

inline std::vector<HeadStrategy> head_strategy_function() {
    std::vector<HeadStrategy> head_strategies(global::N + 1);
    std::vector<int> index(global::N + 1);
    std::iota(index.begin(), index.end(), 0);
    // 优先模拟收益较小的磁盘，此时收益较大的磁盘仍然具有收益，因此可以保证负载均衡
    // std::sort(index.begin() + 1, index.end(),
    //           [&](int i, int j) { return global::disks[i].total_margin_gain < global::disks[j].total_margin_gain; });
    // 貌似干不过随机？
    std::vector<int> simulate_read_time(global::N + 1);
    // 先按照已有策略模拟一次，然后再按照读取次数排序
    for (int i = 1; i <= global::N; i++) {
        auto strategy = single_head_strategy_function(i);
        // 统计有效 read 的数量
        simulate_read_time[i] = 0;
        int p = global::disks[i].head;
        for(int j = 0; j < strategy.actions.size(); j++) {
            if (strategy.actions[j].type == HeadActionType::READ && global::disks[i].query_num[p]) {
                simulate_read_time[i]++;
            }
            p = mod(p, 1, global::V, 1);
        }
    }

    std::sort(index.begin() + 1, index.end(),
              [&simulate_read_time](int i, int j) { return simulate_read_time[i] > simulate_read_time[j]; });
    for (int i = 1; i <= global::N; i++) {
        head_strategies[index[i]] = single_head_strategy_function(index[i]);
        Disk& disk = global::disks[index[i]];
        HeadStrategy& strategy = head_strategies[index[i]];
        // 判断是否将会扫完块并且下一步是否要强制跳转
        int cur_slice = disk.slice_id[disk.head];
        if (!strategy.actions.empty() && strategy.actions[0].type == HeadActionType::JUMP) {
            should_jmp[index[i]] = 0;
        } else if (disk.head + (int)strategy.actions.size() > disk.slice_end[cur_slice]) {
            should_jmp[index[i]] = 1;
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
            object.add_request(req_id, global::timestamp);
            for (int i = 0; i < 3; i++) {
                Disk& disk = global::disks[object.disk_id[i]];
                disk.query_total_object(object.slice_id[i], object.size);
                for (int j = 1; j <= object.size; j++) {
                    disk.query(object.block_id[i][j], global::timestamp);
                }
            }
        }

        // NOTE: 模拟磁盘头动作的任务交给 head_strategy_function
        completed_requests.clear();
        auto head_strategies = head_strategy_function();
        io::read_object_output(head_strategies, completed_requests);
        for (int i = 1; i <= global::N; ++i) {
            global::disks[i].next_timestamp();
        }
    }
}
}  // namespace baseline