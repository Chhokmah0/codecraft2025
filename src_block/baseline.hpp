#include <algorithm>
#include <cassert>
#include <numeric>
#include <random>

#include "global.hpp"
#include "structures.hpp"

namespace baseline {

// int target_disk_id = 1;
std::vector<int> local_disk_empty_block_nums;
std::vector<int> local_disk_p;
std::mt19937 rng(0);

void init_local() { local_disk_p.resize(global::N + 1, 1); }

std::vector<int> put_forward(int disk_id, int size, int start_pos, int object_id, int object_tag) {
    if (start_pos == -1) {
        // start_pos = rng() % global::V + 1;
        start_pos = 1;
        std::vector<int> block_id(size + 1);
        const Disk& disk = global::disks[disk_id];
        int p = start_pos;
        for (int i = 1; i <= size; i++) {
            while (disk.blocks[p].object_id != 0) {
                p = p % global::V + 1;
            }
            global::disks[disk_id].blocks[p].object_id = object_id;
            global::disks[disk_id].use(p, object_tag);
            block_id[i] = p;
            p = p % global::V + 1;
        }
        for (int i = 1; i <= size; i++) {
            assert(block_id[i] != 0);
        }
        return block_id;
    }
    std::vector<int> block_id(size + 1);
    const Disk& disk = global::disks[disk_id];
    int fir = start_pos;
    int end_pos = std::min(fir + disk.part - 1, global::V);
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
        global::disks[disk_id].blocks[p].object_id = object_id;
        global::disks[disk_id].use(p, object_tag);
        block_id[i] = p;
        p = p % global::V + 1;
    }
    assert(min_len_p != -1);
    for (int i = 1; i <= size; i++) {
        assert(block_id[i] != 0);
    }

    return block_id;
}

std::vector<int> put_back(int disk_id, int size, int start_pos, int object_id, int object_tag) {
    std::vector<int> block_id(size + 1);
    const Disk& disk = global::disks[disk_id];
    int& p = start_pos;
    for (int i = 1; i <= size; i++) {
        while (disk.blocks[p].object_id != 0) {
            p = (p - 2 + global::V) % global::V + 1;
        }
        global::disks[disk_id].blocks[p].object_id = object_id;
        global::disks[disk_id].use(p, object_tag);
        block_id[i] = p;
        p = (p - 2 + global::V) % global::V + 1;
    }
    for (int i = 1; i <= size; i++) {
        assert(block_id[i] != 0);
    }
    return block_id;
}
// 根据tag找到一个满足条件的硬盘
int find_disk1(const std::vector<std::pair<int, std::pair<int, int>>>& disk_block, int size, int tag) {
    int now = -1, now_status = 2147483647, res = -1, lst_size = -1;
    for (auto [disk_id, v] : disk_block) {
        int start_pos = v.first, id = v.second;
        int nxt_status = global::disks[disk_id].used_id[id];
        if (global::disks[disk_id].empty_block_size[id] >= size) {
            if (__builtin_popcount(nxt_status) < __builtin_popcount(now_status) && (nxt_status & (1 << tag)) == (1 << tag))
                now = disk_id, now_status = nxt_status, res = start_pos, lst_size = global::disks[disk_id].empty_block_size[id];
            else if (__builtin_popcount(nxt_status) == __builtin_popcount(now_status) && (nxt_status & (1 << tag)) == (1 << tag) && global::disks[disk_id].empty_block_size[id] > lst_size)
                now = disk_id, now_status = nxt_status, res = start_pos, lst_size = global::disks[disk_id].empty_block_size[id];
        }
    }
    // if (now != -1) return now;
    // for (auto [disk_id, v] : disk_block) {
    //     int start_pos = v.first, id = v.second;
    //     int nxt_status = global::disks[disk_id].used_id[id];
    //     if (global::disks[disk_id].empty_block_size[id] >= size) {
    //         if (__builtin_popcount(nxt_status) < __builtin_popcount(now_status))
    //             now = disk_id, now_status = nxt_status, res = start_pos, lst_size = global::disks[disk_id].empty_block_size[id];
    //         else if (__builtin_popcount(nxt_status) == __builtin_popcount(now_status) && global::disks[disk_id].empty_block_size[id] > lst_size)
    //             now = disk_id, now_status = nxt_status, res = start_pos, lst_size = global::disks[disk_id].empty_block_size[id];
    //     }
    // }
    return now;
}
int find_disk2(const std::vector<std::pair<int, std::pair<int, int>>>& disk_block, int size, int tag) {
    int now = -1, now_status = 2147483647, res = -1, lst_size = -1;
    // if (now != -1) return now;
    for (auto [disk_id, v] : disk_block) {
        int start_pos = v.first, id = v.second;
        int nxt_status = global::disks[disk_id].used_id[id];
        if (global::disks[disk_id].empty_block_size[id] >= size) {
            if (__builtin_popcount(nxt_status) < __builtin_popcount(now_status))
                now = disk_id, now_status = nxt_status, res = start_pos, lst_size = global::disks[disk_id].empty_block_size[id];
            else if (__builtin_popcount(nxt_status) == __builtin_popcount(now_status) && global::disks[disk_id].empty_block_size[id] > lst_size)
                now = disk_id, now_status = nxt_status, res = start_pos, lst_size = global::disks[disk_id].empty_block_size[id];
        }
    }
    return now;
}
// 根据tag找到一个满足条件的大块
int find_block(int disk_id, int size, int tag) {
    const Disk& disk = global::disks[disk_id];
    int now = -1, now_status = 2147483647, res = -1, lst_size = -1;
    for (auto [start_pos, id] : disk.div_blocks_id) {
        int nxt_status = disk.used_id[id];
        if (disk.empty_block_size[id] >= size) {
            if (__builtin_popcount(nxt_status) < __builtin_popcount(now_status) && (nxt_status & (1 << tag)) == (1 << tag))
                now = id, now_status = nxt_status, res = start_pos, lst_size = global::disks[disk_id].empty_block_size[id];
            else if (__builtin_popcount(nxt_status) == __builtin_popcount(now_status) && (nxt_status & (1 << tag)) == (1 << tag) && global::disks[disk_id].empty_block_size[id] > lst_size)
                now = id,
                now_status = nxt_status, res = start_pos, lst_size = global::disks[disk_id].empty_block_size[id];
        }
    }
    if (res != -1) return res;
    for (auto [start_pos, id] : disk.div_blocks_id) {
        int nxt_status = disk.used_id[id];
        if (disk.empty_block_size[id] >= size) {
            if (__builtin_popcount(nxt_status) < __builtin_popcount(now_status))
                now = id, now_status = nxt_status, res = start_pos, lst_size = global::disks[disk_id].empty_block_size[id];
            else if (__builtin_popcount(nxt_status) == __builtin_popcount(now_status) && global::disks[disk_id].empty_block_size[id] > lst_size)
                now = id,
                now_status = nxt_status, res = start_pos, lst_size = global::disks[disk_id].empty_block_size[id];
        }
    }
    return res;
}
std::vector<ObjectWriteStrategy> write_strategy(const std::vector<ObjectWriteRequest>& objects) {
    std::vector<ObjectWriteStrategy> strategies(objects.size());
    local_disk_empty_block_nums.clear();
    local_disk_empty_block_nums.push_back(0);
    for (int i = 1; i <= global::N; i++) {
        local_disk_empty_block_nums.push_back(global::disks[i].empty_block_num);
    }

    std::vector<int> object_index(objects.size());
    std::iota(object_index.begin(), object_index.end(), 0);
    // 将具有相同 tag 的元素放在一起。
    std::sort(object_index.begin(), object_index.end(), [&](int i, int j) {
        if (objects[i].tag == objects[j].tag)
            return objects[i].size > objects[j].size;
        return objects[i].tag < objects[j].tag;
    });
    for (int i = 0; i < objects.size(); i++) {
        const ObjectWriteRequest& object = objects[object_index[i]];
        ObjectWriteStrategy& strategy = strategies[object_index[i]];

        strategy.object = object;
        //   把所有硬盘都放进去
        std::vector<int> vs_disk(global::N + 1, 0);
        for (int j = 0; j < 3; j++) {
            std::vector<std::pair<int, std::pair<int, int>>> disk_block;
            for (int disk_id = 1; disk_id <= global::N; ++disk_id)
                if (vs_disk[disk_id] == 0) {
                    const Disk& disk = global::disks[disk_id];
                    for (auto [start_pos, id] : disk.div_blocks_id) {
                        int status = disk.used_id[id];
                        disk_block.push_back({disk_id, {start_pos, id}});
                    }
                }
            std::shuffle(disk_block.begin(), disk_block.end(), rng);
            int target_disk_id = find_disk1(disk_block, object.size, object.tag);
            if (target_disk_id == -1) {
                int max_block_num = 0;
                std::vector<int> disk_list;
                for (int disk_id = 1; disk_id <= global::N; ++disk_id)
                    if (vs_disk[disk_id] == 0) {
                        const Disk& disk = global::disks[disk_id];
                        int empty_block_num = 0;
                        for (auto [start_pos, id] : disk.div_blocks_id) {
                            int status = disk.used_id[id];
                            if (status == 0) empty_block_num++;
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
                // std::shuffle(disk_block.begin(), disk_block.end(), rng);
                if (max_block_num > 0)
                    target_disk_id = disk_list[(long long)rng() % disk_list.size()];
                else
                    target_disk_id = find_disk2(disk_block, object.size, object.tag);
            }
            vs_disk[target_disk_id] = 1;
            assert(target_disk_id != -1);
            strategy.disk_id[j] = target_disk_id;
            local_disk_empty_block_nums[strategy.disk_id[j]] -= object.size;
            int start_pos = find_block(strategy.disk_id[j], object.size, object.tag);
            // 放元素
            // strategy.block_id[j] = put_forward(strategy.disk_id[j], object.size, start_pos == -1 ? local_disk_p[strategy.disk_id[j]] : start_pos, strategy.object.id, strategy.object.tag);
            strategy.block_id[j] = put_forward(strategy.disk_id[j], object.size, start_pos, strategy.object.id, strategy.object.tag);
        }
    }
    return strategies;
}
// 对磁盘进行策略决策/强制跳转
HeadStrategy head_strategy(int disk_id, int should_jmp) {
    const Disk& disk = global::disks[disk_id];
    int head = disk.head;
    HeadActionType pre_action = disk.pre_action;
    int pre_action_cost = disk.pre_action_cost;
    int budget = global::G;

    HeadStrategy strategy;
    if (disk.query.empty()) {
        return strategy;
    }
    strategy.actions.reserve(budget);
    // 只考虑 PASS 和 READ 操作
    while (budget != 0) {
        if (global::get_request_number(disk_id, head) != 0) {
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
        if (head == disk.head) {
            break;
        }
    }
    // 如果策略中没有 READ，JUMP 到下一个需要读取的大块开头
    if (std::all_of(strategy.actions.begin(), strategy.actions.end(),
                    [](const HeadAction& action) { return action.type != HeadActionType::READ; }) ||
        should_jmp) {
        // strategy.actions.clear();
        //   找到需要读取的需求最多的大块开头
        std::vector<std::pair<int, std::pair<int, int>>> request_block;  //<start_pos, block_sum>

        for (int i = 1; i <= global::V; i += disk.part) {
            request_block.push_back({i, {disk.request_block_sum[(i + disk.part - 1) / disk.part], disk.empty_block_size[(i + disk.part - 1) / disk.part]}});
        }
        std::shuffle(request_block.begin(), request_block.end(), rng);
        auto [jmp, block_status] = request_block[0];
        auto [block_sum, empty_block_sum] = block_status;
        for (int i = 1; i < request_block.size(); ++i) {
            auto [new_jmp, new_block_status] = request_block[i];
            auto [new_block_sum, new_empty_block_sum] = new_block_status;
            if (new_block_sum > block_sum) {
                jmp = new_jmp;
                block_sum = new_block_sum;
                empty_block_sum = new_empty_block_sum;
            } else if (new_block_sum == block_sum && new_empty_block_sum < empty_block_sum) {
                jmp = new_jmp;
                block_sum = new_block_sum;
                empty_block_sum = new_empty_block_sum;
            }
        }
        strategy.actions.clear();
        while (global::get_request_number(disk_id, jmp) == 0) jmp = jmp % global::V + 1;
        strategy.add_action(HeadActionType::JUMP, jmp);
    } else {
        // 清空末尾的 pass
        while (!strategy.actions.empty() && strategy.actions.back().type == HeadActionType::PASS) {
            strategy.actions.pop_back();
        }
    }
    return strategy;
}
}  // namespace baseline