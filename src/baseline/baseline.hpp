
#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <functional>
#include <iterator>
#include <numeric>
#include <tuple>
#include <vector>

#include "../io.hpp"
#include "utils.hpp"

namespace baseline {

// 按照 global -> object -> disk 的顺序添加数据
// 按照 disk -> object -> global 的顺序删除数据

// run 函数自动执行了删除对象时的信息维护
// 写入对象、模拟磁头动作的信息维护任务交给 write_strategy_function 和 head_strategy_function

// ---------------策略----------------

inline std::vector<std::array<bool, 2>> should_jmp;  // 每一个 slice 读取完毕后应该强制跳转

inline std::vector<std::vector<int>> suffix_sum_read;  // tag 在每个时间片的后续读取次数
inline std::vector<std::vector<double>> similarity;    // tag 两两之间的相似度

inline int tot_group = 0;                                                 // 当前的 group 数量
inline std::vector<std::array<std::pair<int, int>, 3>> group_disk_slice;  // group_id -> (disk_id, slice_id)x3

// 本地初始化
inline void init_local() {
    for (int i = 0; i <= global::N; i++) {
        global::disks.push_back(Disk(i, global::V, global::M));
    }

    // 三三分组
    std::vector<std::pair<int, int>> temp_disk_slice;
    for (int j = 1; j < global::disks[0].slice_num; j++) {
        for (int i = 1; i <= global::N; i++) {
            temp_disk_slice.push_back({i, j});
        }
    }
    for (int i = 0; i < temp_disk_slice.size(); i += 3) {
        if (i + 2 >= temp_disk_slice.size()) break;
        group_disk_slice.push_back({temp_disk_slice[i], temp_disk_slice[i + 1], temp_disk_slice[i + 2]});
        tot_group++;
    }
    // 最后一个 slice 的长度和前面不一样，需要单独处理
    temp_disk_slice.clear();
    for (int i = 1; i <= global::N; i++) {
        temp_disk_slice.push_back({i, global::disks[i].slice_num});
    }
    // std::shuffle(temp_disk_slice.begin(), temp_disk_slice.end(), global::rng);
    for (int i = 0; i < temp_disk_slice.size(); i += 3) {
        if (i + 2 >= temp_disk_slice.size()) break;
        group_disk_slice.push_back({temp_disk_slice[i], temp_disk_slice[i + 1], temp_disk_slice[i + 2]});
        tot_group++;
    }

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
std::vector<int> put_back(int disk_id, int slice_id, int size) {
    std::vector<int> block_id(size + 1);
    const Disk& disk = global::disks[disk_id];
    // 选择策略：选择最短的能放下size个块的空间
    int fir = disk.slice_start[slice_id];
    int end_pos = disk.slice_end[slice_id];
    int lst = end_pos + 1;  // 修改：从大端开始
    int count = 0;
    int min_len_p = -1, min_len = 2e9;

    while (lst > fir - 1 && count < size) {  // 修改：从大端开始
        lst--;
        if (disk.blocks[lst].object_id == 0) count++;
    }

    for (int i = end_pos;; i--) {  // 修改：从大端开始
        if (lst == fir - 1 && count < size) break;
        if (i - lst < min_len) {  // 修改：计算长度
            min_len = i - lst;
            min_len_p = i;
        }
        if (disk.blocks[i].object_id == 0) count--;
        while (lst > fir - 1 && count < size) {  // 修改：从大端开始
            lst--;
            if (disk.blocks[lst].object_id == 0) count++;
        }
    }

    int p = min_len_p;
    for (int i = 1; i <= size; i++) {
        while (disk.blocks[p].object_id != 0) {
            p = p == 1 ? global::V : p - 1;  // 修改：从大端开始
        }
        block_id[i] = p;
        p = p == 1 ? global::V : p - 1;  // 修改：从大端开始
    }

    for (int i = 1; i <= size; i++) {
        assert(block_id[i] != 0);
    }
    return block_id;
}

// 写入策略函数，需要维护 object 和 disk 的状态
inline std::vector<ObjectWriteStrategy> write_strategy_function(const std::vector<ObjectWriteRequest>& objects) {
    std::vector<ObjectWriteStrategy> strategies(objects.size());

    std::vector<int> object_index(objects.size());
    std::iota(object_index.begin(), object_index.end(), 0);
    // 给一个 object 的打分函数
    auto object_key = [&](int i) {
        int time_block = std::min((global::timestamp - 1) / 1800 + 1, global::fre_len);
        int read_count = suffix_sum_read[objects[i].tag][time_block];
        int size = objects[i].size;
        return std::make_tuple(size, read_count, objects[i].tag);
    };
    std::sort(object_index.begin(), object_index.end(), [&](int i, int j) {
        // FIXME: 这里的实现有点问题，但不知道为啥有用
        return object_key(i) < object_key(j);
    });

    for (size_t opt = 0; opt < object_index.size(); opt++) {
        const ObjectWriteRequest& object = objects[object_index[opt]];
        ObjectWriteStrategy& strategy = strategies[object_index[opt]];

        strategy.object = object;
        std::vector<int> group_ids;
        // 提取出可以放置该物品的 group
        for (int i = 0; i < tot_group; i++) {
            int disk_id = group_disk_slice[i][0].first;
            int slice_id = group_disk_slice[i][0].second;
            Disk& disk = global::disks[disk_id];
            if (disk.slice_empty_block_num[slice_id] >= object.size) {
                group_ids.push_back(i);
            }
        }

        if (group_ids.empty()) {
            // 应该不会出现这种情况
            throw std::runtime_error("No disk can be used.");
        }

        // 按照某些优先级给 group 排序
        auto group_key = [&](const int& group_id) {
            struct GroupValue {
                bool has_tag;
                int tag_num;
                int empty_block_num;
                int empty_slice_num;

                bool operator<(const GroupValue& other) const {
                    // 优先拥有 tag
                    if (has_tag != other.has_tag) {
                        return has_tag > other.has_tag;
                    }
                    // 优先选择 tag 数量少的 slice
                    if (tag_num != other.tag_num) {
                        return tag_num < other.tag_num;
                    }
                    // 优先选择空闲块数多的 slice
                    if (empty_block_num != other.empty_block_num) {
                        return empty_block_num > other.empty_block_num;
                    }
                    // 优先选择空闲 slice 多的 disk
                    if (empty_slice_num != other.empty_slice_num) {
                        return empty_slice_num > other.empty_slice_num;
                    }
                    return false;
                }
            };
            int disk_id = group_disk_slice[group_id][0].first;
            int slice_id = group_disk_slice[group_id][0].second;
            Disk& disk = global::disks[disk_id];
            // 计算 slice 的信息
            bool has_tag = (disk.slice_tag[slice_id] & (1 << object.tag)) == (1 << object.tag);
            int tag_num = __builtin_popcount(disk.slice_tag[slice_id]);
            int empty_block_num = disk.slice_empty_block_num[slice_id];

            // 由于三三分组，这里选择三个 slice 所在 disk 中拥有最少 slice 数的硬盘作为参考
            int min_empty_slice_num = global::disks[disk_id].slice_num;
            for (int i = 0; i < 3; i++) {
                int disk_id = group_disk_slice[group_id][i].first;
                Disk& disk = global::disks[disk_id];
                int empty_slice_num = 0;
                for (int j = 1; j <= disk.slice_num; j++) {
                    if (disk.slice_empty_block_num[j] == disk.slice_end[j] - disk.slice_start[j] + 1) {
                        empty_slice_num++;
                    }
                }
                min_empty_slice_num = std::min(min_empty_slice_num, empty_slice_num);
            }

            return GroupValue{has_tag, tag_num, empty_block_num, min_empty_slice_num};
        };
        auto slice_cmp = [&](const int& group_id1, const int& group_id2) {
            return group_key(group_id1) < group_key(group_id2);
        };
        std::shuffle(group_ids.begin(), group_ids.end(), global::rng);
        auto it = std::min_element(group_ids.begin(), group_ids.end(), slice_cmp);
        // 选出最优的 group_id
        auto group_id = *it;

        // 选好了硬盘和 slice，开始放置
        for (int i = 0; i < 3; i++) {
            int disk_id = group_disk_slice[group_id][i].first;
            int slice_id = group_disk_slice[group_id][i].second;
            strategy.disk_id[i] = disk_id;
            strategy.slice_id[i] = slice_id;
            strategy.block_id[i] = put_forward(disk_id, slice_id, object.size);
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
    if (disk.total_request_num == 0) {
        return strategy;
    }
    if (should_jmp[disk_id][head_id]) {
        // 跳转到收益最大的 slice 的可读取开头
        std::vector<double> slice_gain(disk.slice_num + 1);
        for (int i = 1; i <= disk.slice_num; i++) {
            slice_gain[i] = disk.get_slice_gain(i);
        }
        int max_slice_id = std::max_element(slice_gain.begin() + 1, slice_gain.end()) - slice_gain.begin();

        int target_slice = max_slice_id;
        int target = disk.slice_start[target_slice];
        while (disk.request_num[target] == 0) {
            target = mod(target, 1, global::V, 1);
            // target = mod(target, disk.slice_start[target_slice], disk.slice_end[target_slice], 1);
            // if (target == disk.slice_start[target_slice]) {
            //     break;
            // }
        }
        strategy.add_action(HeadActionType::JUMP, target);
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
            if (disk.request_num[p] == 0) {
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
        if (disk.request_num[p] == 0) {
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
    return strategy;
}

// 具体的磁头策略，需要维护 disk 的状态
inline std::vector<std::array<HeadStrategy, 2>> head_strategy_function() {
    std::vector<std::array<HeadStrategy, 2>> head_strategies(global::N + 1);
    // 优先模拟收益较小的磁盘，此时收益较大的磁盘仍然具有收益，因此可以保证负载均衡
    // std::sort(index.begin() + 1, index.end(),
    //           [&](int i, int j) { return global::disks[i].total_margin_gain < global::disks[j].total_margin_gain; });
    // 貌似干不过随机？
    // 先按照策略模拟一次，如果没有有效读入，则额外标记为需要跳转
    for (int disk_id = 1; disk_id <= global::N; disk_id++) {
        for (int head_id = 0; head_id < 2; head_id++) {
            head_strategies[disk_id][head_id] = simulate_strategy(disk_id, head_id);
            HeadStrategy& strategy = head_strategies[disk_id][head_id];
            if (strategy.actions.empty()) {
                should_jmp[disk_id][head_id] = true;
            }
        }
    }
    // 清理所有不需要跳转的磁头往后的 block 贡献
    for (int disk_id = 1; disk_id <= global::N; disk_id++) {
        for (int head_id = 0; head_id < 2; head_id++) {
            if (should_jmp[disk_id][head_id]) {
                continue;
            }
            clean_gain_after_head(global::disks[disk_id], global::disks[disk_id].head[head_id]);
        }
    }
    std::vector<int> index(2 * global::N + 1);
    std::iota(index.begin(), index.end(), 0);
    std::vector<int> simulate_read_time(2 * global::N + 1);
    // 先按照已有策略模拟一次，然后再按照读取次数排序
    for (int i = 1; i <= global::N; i++) {
        HeadStrategy strategy1 = simulate_strategy(i, 0), strategy2 = simulate_strategy(i, 1);
        simulate_read_time[i] =
            std::count_if(strategy1.actions.begin(), strategy1.actions.end(),
                          [](const HeadAction& action) { return action.type == HeadActionType::READ; });
        simulate_read_time[i + global::N] =
            std::count_if(strategy2.actions.begin(), strategy2.actions.end(),
                          [](const HeadAction& action) { return action.type == HeadActionType::READ; });
    }
    std::sort(index.begin() + 1, index.end(),
              [&simulate_read_time](int i, int j) { return simulate_read_time[i] > simulate_read_time[j]; });
    for (int i = 1; i <= 2 * global::N; i++) {
        int disk_id = index[i] > global::N ? index[i] - global::N : index[i];
        int head_id = index[i] > global::N ? 1 : 0;
        Disk& disk = global::disks[disk_id];
        head_strategies[disk_id][head_id] = simulate_strategy(disk_id, head_id);
        HeadStrategy& strategy = head_strategies[disk_id][head_id];
        // 判断是否已经扫完块并且下一步是否要强制跳转
        // 如果 strategy.actions.size() + disk.head[head_id] 大于最后一个有查询的块，那么下一个时间片就可以跳转
        int slice_last_query_p = disk.slice_end[disk.slice_id[disk.head[head_id]]];
        for (int i = disk.slice_end[disk.slice_id[disk.head[head_id]]];
             i >= disk.slice_start[disk.slice_id[disk.head[head_id]]]; i--) {
            if (disk.request_num[i] != 0) {
                slice_last_query_p = i;
                break;
            }
        }
        if (!strategy.actions.empty() && strategy.actions[0].type == HeadActionType::JUMP) {
            clean_gain_after_head(disk, strategy.actions[0].target);
            should_jmp[disk_id][head_id] = false;
        } else if ((int)strategy.actions.size() + disk.head[head_id] > slice_last_query_p) {
            should_jmp[disk_id][head_id] = true;
        }
        // 模拟磁头动作
        simulate_head(disk, head_id, strategy);
    }
    return head_strategies;
}

// -------------------------放弃读取请求-------------------------

// 放弃读取请求，需要维护 disk 和 object 的状态
inline std::vector<int> timeout_read_requests_function() {
    std::vector<int> timeout_read_requests;
    for (auto& [obj_id, object] : global::objects) {
        int predict_time = 105;      // 需要被丢掉的预测时间
        int used_time = 0x3f3f3f3f;  // 读取该物品所需要的最小时间
        for (int i = 0; i < 3; i++) {
            Disk& disk = global::disks[object.disk_id[i]];
            int disk_used_time = 0x3f3f3f3f;
            if (disk.slice_id[disk.head[0]] == disk.slice_id[object.block_id[i][1]]) {
                disk_used_time = 0;
            } else if (disk.slice_id[disk.head[1]] == disk.slice_id[object.block_id[i][1]]) {
                disk_used_time = 0;
            } else {
                disk_used_time = 1 + (object.size * 16 + global::G - 1 + object.block_id[i][object.size] -
                                      disk.slice_start[disk.slice_id[object.block_id[i][1]]]) /
                                         global::G;
            }
            used_time = std::min(used_time, disk_used_time);
        }
        predict_time -= used_time;
        auto temp_timeout_read_requests = object.get_timeout_requests(global::timestamp, predict_time);
        for (auto req_id : temp_timeout_read_requests) {
            timeout_read_requests.push_back(req_id);
            give_up_request(req_id);
        }
    }
    return timeout_read_requests;
}

inline std::vector<std::vector<std::pair<int, int>>> garbage_collection_function() {
    std::vector<std::vector<std::pair<int, int>>> garbage_collection_strategies(global::N + 1);
    for (int i = 1; i <= global::N; i++) {
        // std::cerr << i << "\n";
        Disk& disk = global::disks[i];
        std::vector<std::pair<int, int>> cand;
        for (int j = 1; j <= disk.slice_num; j++) {
            if (disk.slice_tag[j] == 0) {
                continue;
            }
            if (disk.slice_id[disk.head[0]] == j || disk.slice_id[disk.head[1]] == j) {
                continue;
            }
            std::vector<int> bubble;
            std::vector<int> data;
            for (int k = disk.slice_start[j]; k <= disk.slice_end[j]; k++) {
                if (disk.blocks[k].object_id != 0) {
                    data.push_back(k);
                }
            }
            if (data.empty()) {
                continue;
            }
            for (int k = data.back(); k >= disk.slice_start[j]; --k) {
                if (disk.blocks[k].object_id == 0) {
                    bubble.push_back(k);
                }
            }
            std::reverse(bubble.begin(), bubble.end());
            std::reverse(data.begin(), data.end());
            for (int k = 0; k < std::min(bubble.size(), data.size()); ++k) {
                if (bubble[k] >= data[k]) break;
                cand.push_back({bubble[k], data[k]});
            }
        }
        std::sort(cand.begin(), cand.end(), [&](const std::pair<int, int>& a, const std::pair<int, int>& b) {
            double gain_a = disk.get_slice_gain(disk.slice_id[a.first]);
            double gain_b = disk.get_slice_gain(disk.slice_id[b.first]);
            if (a.second - a.first != b.second - b.first) {
                return a.second - a.first > b.second - b.first;
            }
            if (gain_a != gain_b) {
                return gain_a > gain_b;
            }
            return false;
        });
        for (int j = 0; j < std::min(global::K, (int)cand.size()); ++j) {
            garbage_collection_strategies[i].push_back(cand[j]);
            swap_block(disk, cand[j].first, cand[j].second);
        }
    }
    return garbage_collection_strategies;
}
// ---------------交互----------------
// 应该是不需要修改
inline void run() {
    io::init_input();
    init_local();
    io::init_output();
    int busy_request_num = 0, done_request_num = 0;  // 一个统计有多少查询被busy,完成了多少的变量。
    for (global::timestamp = 1; global::timestamp <= global::T + 105; global::timestamp++) {
        // debug
        // std::cerr << "timestamp: " << global::timestamp << '\n';
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
                disk.query(object, req_id);
            }
            global::request_object_id[req_id] = object_id;
        }
        // NOTE: 模拟磁盘头动作的任务交给 head_strategy_function
        completed_requests.clear();
        auto head_strategies = head_strategy_function();
        io::read_object_output(head_strategies, completed_requests);
        done_request_num += completed_requests.size();
        // 获取放弃/超时的读取请求
        auto timeout_read_requests = timeout_read_requests_function();
        busy_request_num += (int)timeout_read_requests.size();
        io::busy_requests_output(timeout_read_requests);
        // 一轮结束，更新磁盘的状态
        for (int i = 1; i <= global::N; ++i) {
            auto timeout_requests = global::disks[i].next_time();
        }
        // 垃圾回收
        // TODO: 临时方案
        if (global::timestamp % 1800 == 0) {
            std::cerr << global::timestamp << " total busy:" << busy_request_num << ",total done: " << done_request_num
                      << '\n';
            std::cerr.flush();
            io::garbage_collection_input();
            auto garbage_collection_strategies = garbage_collection_function();
            io::garbage_collection_output(garbage_collection_strategies);
            // io::garbage_collection_output(std::vector<std::vector<std::pair<int, int>>>(global::N + 1));
        }
    }
    std::cerr << "final total busy:" << " " << busy_request_num << ",total done: " << done_request_num << '\n';
    std::cerr.flush();
}
}  // namespace baseline
