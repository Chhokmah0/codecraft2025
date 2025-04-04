#pragma once

#include <algorithm>
#include <cassert>
#include <functional>
#include <iomanip>
#include <iterator>
#include <numeric>
#include <vector>

#include "../io.hpp"
#include "utils.hpp"

namespace baseline {

// run 函数自动执行了删除对象时的信息维护
// 写入对象、模拟磁头动作的信息维护任务交给 write_strategy_function 和 head_strategy_function

// ---------------策略----------------

inline std::vector<std::array<bool, 2>> should_jmp;  // 每一个 slice 读取完毕后应该强制跳转

inline std::vector<std::vector<int>> suffix_sum_read;  // tag 在每个时间片的后续读取次数
inline std::vector<std::vector<double>> similarity;    // tag 两两之间的相似度

// 本地初始化
inline void init_local() {
    global::disks.resize(global::N + 1, Disk(global::V, global::M));
    should_jmp.resize(global::N + 1);

    suffix_sum_read = global::fre_read;
    for (int i = 1; i <= global::M; i++) {
        for (int j = global::fre_len; j >= 1; j--) {
            suffix_sum_read[i][j - 1] += suffix_sum_read[i][j];
        }
    }

    // 余弦相似度
    similarity.resize(global::M + 1, std::vector<double>(global::M + 1));
    for (int i = 1; i <= global::M; i++) {
        for (int j = 1; j <= global::M; j++) {
            double p = 0, lx = 0, ly = 0;
            for (int k = 1; k <= global::fre_len; k++) {
                p += (double)global::fre_read[i][k] * global::fre_read[j][k];
                lx += (double)global::fre_read[i][k] * global::fre_read[i][k];
                ly += (double)global::fre_read[j][k] * global::fre_read[j][k];
            }
            similarity[i][j] = p / std::sqrt(lx * ly);
        }
        similarity[i][i] = 1;  // 减少精度损失
    }
}

inline double similarity_with_slice(const Disk& disk, int slice_id, int tag) {
    size_t total_writed_num =
        std::accumulate(disk.slice_tag_writed_num[slice_id].begin(), disk.slice_tag_writed_num[slice_id].end(), 0);
    // 加权平均
    double similarity_sum = 0;
    for (int i = 1; i <= global::M; i++) {
        if (disk.slice_tag_writed_num[slice_id][i] != 0) {
            similarity_sum += (double)disk.slice_tag_writed_num[slice_id][i] / total_writed_num * similarity[tag][i];
        }
    }
    return similarity_sum;
}
inline double similarity_with_slice(int disk_id, int slice_id, int tag) {
    return similarity_with_slice(global::disks[disk_id], slice_id, tag);
}

// -------------------------写入策略-------------------------
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
// 首先去所有slices里找拥有当前tag且slice里tag数量最少的，数量相同取剩余空间最大的，仍然相同就随机选一个
inline int find_disk1(const std::vector<std::pair<int, int>>& disk_block, int size, int tag) {
    int now = -1, now_status = 2147483647, lst_size = -1;
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
// 在这个硬盘里找tag数量最少的slice，如果相同找剩余空间最大的，仍然相同就随机
inline int find_disk2(const std::vector<std::pair<int, int>>& disk_block, int size, int tag) {
    int now = -1, now_status = 2147483647, lst_size = -1;
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
    int now = 0, now_status = 2147483647, res = -1, lst_size = -1;
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

// 写入策略函数，需要维护 object 和 disk 的状态
inline std::vector<ObjectWriteStrategy> write_strategy_function(const std::vector<ObjectWriteRequest>& objects) {
    std::vector<ObjectWriteStrategy> strategies(objects.size());

    std::vector<int> object_index(objects.size());
    std::iota(object_index.begin(), object_index.end(), 0);
    // 将具有相同 tag 的元素放在一起
    // 如果一个 tag 在之后的读取次数最多，则优先放置
    // 并优先放大的
    std::sort(object_index.begin(), object_index.end(), [&](int i, int j) {
        int time_block = std::min((global::timestamp - 1) / 1800 + 1, global::fre_len);
        int read_count_i = suffix_sum_read[objects[i].tag][time_block];
        int read_count_j = suffix_sum_read[objects[j].tag][time_block];
        // FIXME: 这里的实现有点问题，但不知道为啥有用
        return std::tie(read_count_i, objects[i].tag, objects[i].size) <
               std::tie(read_count_j, objects[j].tag, objects[j].size);
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
            int target_slice_id = -1;
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
                int max_slice_num = 0;
                std::vector<int> disk_list;
                for (auto disk_id : disk_ids) {
                    // 不能是已经用了的硬盘
                    if (strategy.is_used_disk(disk_id)) {
                        continue;
                    }

                    Disk& disk = global::disks[disk_id];
                    int empty_slice_num = 0;
                    for (int slice_id = 1; slice_id <= disk.slice_num; slice_id++) {
                        int status = disk.slice_tag[slice_id];
                        if (status == 0) empty_slice_num++;
                        // if (disk.slice_empty_block_num[slice_id] >= object.size) {
                        //     empty_block_num += disk.slice_empty_block_num[slice_id];
                        // }待尝试的方向
                    }
                    if (empty_slice_num > max_slice_num) {
                        max_slice_num = empty_slice_num;
                        disk_list.clear();
                    }
                    if (empty_slice_num == max_slice_num) {
                        disk_list.push_back(disk_id);
                    }
                }
                // 优先空的块尽可能多的盘
                std::shuffle(disk_list.begin(), disk_list.end(), global::rng);
                std::shuffle(disk_block.begin(), disk_block.end(), global::rng);
                if (max_slice_num > 0)
                    target_disk_id = disk_list[global::rng() % disk_list.size()];
                else  // 没有为空的硬盘，选tag最少的slice
                    target_disk_id = find_disk2(disk_block, object.size, object.tag);
            }

            if (target_disk_id == -1) {
                // 应该不会出现这种情况
                throw std::runtime_error("No disk can be used.");
            }
            // 选好了硬盘和 slice，开始放置
            strategy.disk_id[i] = target_disk_id;
            target_slice_id = find_slice(strategy.disk_id[i], object.size, object.tag);
            // if (target_disk_id % 2 == 1) {
            strategy.block_id[i] = put_forward(target_disk_id, target_slice_id, object.size);
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

// -------------------------磁头策略-------------------------
// 磁头策略函数，返回 disk_id 磁头的策略
inline HeadStrategy simulate_strategy(int disk_id, int head_id) {
    Disk& disk = global::disks[disk_id];
    HeadStrategy strategy;
    if (disk.total_query_num == 0) {
        return strategy;
    }
    // 第 n 次 READ 时所消耗的令牌，第 8 次往后都是 16
    // 0 是 PASS 时的消耗
    const int COST[] = {1, 64, 52, 42, 34, 28, 23, 19, 16};
    const int COST_SIZE = sizeof(COST) / sizeof(int);

    // 动态规划
    // dp[i][j] 表示磁头在位置 i 时，使用第 j 次 READ 后的最大剩余令牌
    std::vector<std::array<int, COST_SIZE>> dp;
    std::vector<std::array<int, COST_SIZE>> pre_read_count;
    for (int i = 0, p = disk.head[head_id]; i <= global::V; i++) {
        dp.push_back(std::array<int, COST_SIZE>());
        dp.back().fill(-1);
        pre_read_count.push_back(std::array<int, COST_SIZE>());
        pre_read_count.back().fill(0);
        // 开始处的处理
        if (i == 0) {
            int read_count;
            if (disk.pre_action[head_id] == HeadActionType::READ) {
                read_count = std::lower_bound(std::begin(COST) + 1, std::end(COST), disk.pre_action_cost[head_id],
                                              std::greater<int>()) -
                             COST;
            } else {
                read_count = 0;
            }
            if (disk.query_num[p] == 0) {
                // 磁头在空闲块上时才可以 PASS
                dp[0][0] = global::G - 1;
                pre_read_count[0][0] = read_count;
            }
            // READ 操作
            if (read_count == COST_SIZE - 1) {
                dp[0][read_count] = global::G - COST[read_count];
                pre_read_count[0][read_count] = read_count;
            } else {
                dp[0][read_count + 1] = global::G - COST[read_count + 1];
                pre_read_count[0][read_count + 1] = read_count;
            }
            p = mod(p, 1, global::V, 1);
            continue;
        }

        // 计算 dp[i][j]
        if (disk.query_num[p] == 0) {
            // 磁头在空闲块上时才可以 PASS
            int max_budget_read_count = std::max_element(dp[i - 1].begin(), dp[i - 1].end()) - dp[i - 1].begin();
            int max_budget = dp[i - 1][max_budget_read_count];
            if (max_budget != 0) {
                if (max_budget - COST[0] > dp[i][0]) {
                    dp[i][0] = max_budget - COST[0];
                    pre_read_count[i][0] = max_budget_read_count;
                }
            }
        }
        // READ 操作
        for (int j = 0; j < COST_SIZE; j++) {
            if (dp[i - 1][j] == -1) continue;
            if (j == COST_SIZE - 1) {
                if (dp[i - 1][j] - COST[j] > dp[i][j]) {
                    dp[i][j] = dp[i - 1][j] - COST[j];
                    pre_read_count[i][j] = j;
                }
            } else {
                if (dp[i - 1][j] - COST[j + 1] > dp[i][j + 1]) {
                    dp[i][j + 1] = dp[i - 1][j] - COST[j + 1];
                    pre_read_count[i][j + 1] = j;
                }
            }
        }
        if (std::all_of(dp[i].begin(), dp[i].end(), [](int x) { return x == -1; })) {
            // 如果所有的 dp[i][j] 都是 -1，说明没有可用的令牌
            dp.pop_back();
            pre_read_count.pop_back();
            break;
        }
        p = mod(p, 1, global::V, 1);
    }
    // 获取 dp 的方案
    if (!dp.empty()) {
        int dp_p = dp.size() - 1;
        int dp_j = std::max_element(dp.back().begin(), dp.back().end()) - dp.back().begin();
        while (dp_p >= 0) {
            if (dp_j == 0) {
                strategy.add_action(HeadActionType::PASS);
            } else {
                strategy.add_action(HeadActionType::READ);
            }
            dp_j = pre_read_count[dp_p][dp_j];
            dp_p--;
        }
        std::reverse(strategy.actions.begin(), strategy.actions.end());
    }

    // 清空末尾的 pass
    while (!strategy.actions.empty() && strategy.actions.back().type == HeadActionType::PASS) {
        strategy.actions.pop_back();
    }
    // 检测是否有有效的 read
    bool valid_strategy = false;
    int p = disk.head[head_id];
    for (const auto& action : strategy.actions) {
        if (action.type == HeadActionType::READ && disk.query_num[p] != 0) {
            valid_strategy = true;
            break;
        }
        p = mod(p, 1, global::V, 1);
    }

    // 如果策略中没有有效的 READ，贪心 JUMP 到下一个收益最大的 slice 的可读取开头
    if (!valid_strategy || should_jmp[disk_id][head_id]) {
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
            while (disk.query_num[target] == 0) {
                target = mod(target, 1, global::V, 1);
            }
            strategy.add_action(HeadActionType::JUMP, target);
        }
    }
    return strategy;
}

// 具体的磁头策略，需要维护 disk 的状态
inline std::vector<std::array<HeadStrategy, 2>> head_strategy_function() {
    std::vector<std::array<HeadStrategy, 2>> head_strategies(global::N + 1);
    std::vector<int> index(2 * global::N + 1);
    std::iota(index.begin(), index.end(), 0);
    // 优先模拟收益较小的磁盘，此时收益较大的磁盘仍然具有收益，因此可以保证负载均衡
    // std::sort(index.begin() + 1, index.end(),
    //           [&](int i, int j) { return global::disks[i].total_margin_gain < global::disks[j].total_margin_gain; });
    // 貌似干不过随机？
    std::vector<int> simulate_read_time(2 * global::N + 1);
    // 先按照已有策略模拟一次，然后再按照读取次数排序
    for (int i = 1; i <= global::N; i++) {
        HeadStrategy strategy1 = simulate_strategy(i, 0), strategy2 = simulate_strategy(i, 1);
        simulate_read_time[i] =
            std::count_if(strategy1.actions.begin(), strategy1.actions.end(),
                          [](const HeadAction& action) { return action.type == HeadActionType::READ; });
<<<<<<< HEAD
        simulate_read_time[i + global::N] = std::count_if(strategy2.actions.begin(), strategy2.actions.end(),
                                                          [](const HeadAction& action) { return action.type == HeadActionType::READ; });
=======
        simulate_read_time[i + global::N] =
            std::count_if(strategy2.actions.begin(), strategy2.actions.end(),
                          [](const HeadAction& action) { return action.type == HeadActionType::READ; });
>>>>>>> 7c57a7013fc3b3cd743810cd6cda82f9e2611ae0
    }

    std::sort(index.begin() + 1, index.end(),
              [&simulate_read_time](int i, int j) { return simulate_read_time[i] > simulate_read_time[j]; });
    for (int i = 1; i <= 2 * global::N; i++) {
<<<<<<< HEAD
        // for (int head_id = 0; head_id < 2; head_id++) {
=======
>>>>>>> 7c57a7013fc3b3cd743810cd6cda82f9e2611ae0
        int disk_id = index[i] > global::N ? index[i] - global::N : index[i];
        int head_id = index[i] > global::N ? 1 : 0;
        Disk& disk = global::disks[disk_id];
        head_strategies[disk_id][head_id] = simulate_strategy(disk_id, head_id);
        HeadStrategy& strategy = head_strategies[disk_id][head_id];
        // 判断是否已经扫完块并且下一步是否要强制跳转
        if (!strategy.actions.empty() && strategy.actions[0].type == HeadActionType::JUMP) {
            should_jmp[disk_id][head_id] = false;
        } else if ((int)strategy.actions.size() + disk.head[head_id] >
                   disk.slice_end[disk.slice_id[disk.head[head_id]]]) {
            should_jmp[disk_id][head_id] = true;
        }
        // 模拟磁头动作
        simulate_head(disk, head_id, strategy);
        // 如果是跳转的话，将该块对应的其他块的信息清空
        if (!strategy.actions.empty() && strategy.actions[0].type == HeadActionType::JUMP) {
<<<<<<< HEAD
            for (int pos = strategy.actions[0].target;
                 pos != disk.slice_end[disk.slice_id[strategy.actions[0].target]]; pos++) {
=======
            for (int pos = strategy.actions[0].target; pos != disk.slice_end[disk.slice_id[strategy.actions[0].target]];
                 pos++) {
>>>>>>> 7c57a7013fc3b3cd743810cd6cda82f9e2611ae0
                ObjectBlock& block = disk.blocks[pos];
                if (block.object_id == 0 || disk.query_num[pos] == 0) continue;
                Object& object = global::objects[block.object_id];
                object.clean_gain();
                for (int i = 0; i < 3; i++) {
                    Disk& diskt = global::disks[object.disk_id[i]];
                    for (int j = 1; j <= object.size; j++) {
                        diskt.clean_gain(object.block_id[i][j]);
                    }
                }
            }
            assert(std::abs(disk.slice_margin_gain[disk.slice_id[strategy.actions[0].target]]) <= 1e-10);
        }
    }
    return head_strategies;
}

// -------------------------放弃读取请求-------------------------

// 放弃读取请求，需要维护 disk 和 object 的状态
inline std::vector<int> timeout_read_requests_function() {
    std::vector<int> timeout_read_requests;
    for (auto& [obj_id, object] : global::objects) {
        // 获取时就维护了 object 的状态
        auto temp_timeout_read_requests = object.get_timeout_requests(global::timestamp);
        for (const auto& request : temp_timeout_read_requests) {
            timeout_read_requests.push_back(request.req_id);
            // 维护磁盘的状态
            for (int i = 0; i < 3; i++) {
                Disk& disk = global::disks[object.disk_id[i]];
                for (int j = 1; j <= object.size; j++) {
                    if (request.readed[j] == false) {
                        disk.decease_query(object.block_id[i][j]);
                        if (request.uncleand_gain) {
                            disk.decease_gain(object.block_id[i][j], global::timestamp - request.timestamp);
                        }
                    }
                }
            }
        }
    }
    return timeout_read_requests;
}

// ---------------交互----------------
// 应该是不需要修改
inline void run() {
    io::init_input();
    init_local();
    io::init_output();
    for (global::timestamp = 1; global::timestamp <= global::T + 105; global::timestamp++) {
        // debug
        // std::cerr << "timestamp: " << global::timestamp << std::endl;
        // for (int i = 1; i <= global::N; ++i) {
        //     global::disks[i].debug_check();
        // }
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
                for (int j = 1; j <= object.size; j++) {
                    disk.query(object.block_id[i][j], global::timestamp);
                }
            }
        }

        // NOTE: 模拟磁盘头动作的任务交给 head_strategy_function
        completed_requests.clear();
        auto head_strategies = head_strategy_function();
        io::read_object_output(head_strategies, completed_requests);

        // 获取放弃/超时的读取请求
        auto timeout_read_requests = timeout_read_requests_function();
        io::busy_requests_output(timeout_read_requests);
        // 一轮结束，更新磁盘的状态
        for (int i = 1; i <= global::N; ++i) {
            for (int j = 1; j <= global::V; ++j) {
                if (global::disks[i].blocks[j].object_id != 0) {
                    global::disks[i].update(j);
                }
            }
        }

        // 垃圾回收
        // TODO: 临时方案
        if (global::timestamp % 1800 == 0) {
            io::garbage_collection_input();
            io::garbage_collection_output();
        }
    }
}
}  // namespace baseline