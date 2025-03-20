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
    std::vector<int> block_id(size + 1);
    const Disk& disk = global::disks[disk_id];
    int& p = start_pos;
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
int find_disk(std::vector<std::pair<int, std::pair<int, int>>> disk_block, int size, int tag) {
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
    if (now != -1) return now;
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
    std::sort(object_index.begin(), object_index.end(), [&](int i, int j) { return objects[i].tag < objects[j].tag; });

    for (int i = 0; i < objects.size(); i++) {
        const ObjectWriteRequest& object = objects[object_index[i]];
        ObjectWriteStrategy& strategy = strategies[object_index[i]];

        strategy.object = object;
        //std::vector<std::pair<int, std::pair<int, int>>> disk_block;
        // for (int disk_id = 1; disk_id <= global::N; ++disk_id) {
        //     const Disk& disk = global::disks[disk_id];
        //     int empty_block_num = 0;
        //     for (auto [start_pos, id] : disk.div_blocks_id) {
        //         int status = disk.used_id[id];
        //         if (status == 0) empty_block_num++;
        //         disk_block.push_back({disk_id, {start_pos, id}});
        //     }
        // }
        // std::shuffle(disk_block.begin(), disk_block.end(), rng);
        //  把所有硬盘都放进去
        std::vector<int> vs_disk(global::N + 1,0);
        for (int j = 0; j < 3; j++) {
            std::vector<std::pair<int, std::pair<int, int>>> disk_block;
            int min_block_num=0;
            for (int disk_id = 1; disk_id <= global::N; ++disk_id) if(vs_disk[disk_id]==0){
                const Disk& disk = global::disks[disk_id];
                int empty_block_num = 0;
                for (auto [start_pos, id] : disk.div_blocks_id) {
                    int status = disk.used_id[id];
                    if (status == 0) empty_block_num++;
                }
                if(empty_block_num>min_block_num){
                    min_block_num=empty_block_num;
                    disk_block.clear();
                }
                if(empty_block_num == min_block_num){
                    for (auto [start_pos, id] : disk.div_blocks_id) {
                        int status = disk.used_id[id];
                        disk_block.push_back({disk_id, {start_pos, id}});
                    }
                }
            }           
            std::shuffle(disk_block.begin(), disk_block.end(), rng);
            int target_disk_id = find_disk(disk_block, object.size, object.tag);
            vs_disk[target_disk_id]=1;
            assert(target_disk_id != -1);
            strategy.disk_id[j] = target_disk_id;
            std::vector<std::pair<int, std::pair<int, int>>> new_disk_block;
            for (auto [disk_id, t] : disk_block) {
                if (target_disk_id != disk_id) {
                    new_disk_block.push_back({disk_id, t});
                }
            }
            disk_block = new_disk_block;
            local_disk_empty_block_nums[strategy.disk_id[j]] -= object.size;
            int start_pos = find_block(strategy.disk_id[j], object.size, object.tag);
            // 选定块，根据硬盘的奇偶性质确定放置方向
            //if (strategy.disk_id[j] % 2 == 1) {
            strategy.block_id[j] = put_forward(strategy.disk_id[j], object.size, start_pos == -1 ? local_disk_p[strategy.disk_id[j]] : start_pos, strategy.object.id, strategy.object.tag);
            // } else {
            //     strategy.block_id[j] = put_back(strategy.disk_id[j], object.size, start_pos == -1 ? local_disk_p[strategy.disk_id[j]] : std::min(start_pos + global::disks[target_disk_id].part - 1, global::V), strategy.object.id, strategy.object.tag);
            // }
        }

        // 保证第二个硬盘上的顺序和第一个硬盘上不一样（put_back 本身就会反向放置）
        // if (strategy.disk_id[1] % 2 == strategy.disk_id[0] % 2) {
        //     std::reverse(strategy.block_id[1].begin() + 1, strategy.block_id[1].end());
        // }

        // 随机打乱第三个硬盘上的顺序
        // tmp[i]是第i个盘放哪个对象块
        // std::vector<int> tmp(object.size + 1);
        // int pl, pr;
        // int ps = 0;
        // if (strategy.block_id[2].size() % 2 == 0) {
        //     pl = strategy.block_id[2].size() / 2 - 1;
        //     pr = strategy.block_id[2].size() / 2 + 1;
        //     tmp[pl + 1] = strategy.block_id[2][++ps];
        // } else {
        //     pl = strategy.block_id[2].size() / 2;
        //     pr = strategy.block_id[2].size() / 2 + 1;
        // }
        // while (pl >= 1 && pr < strategy.block_id[2].size()) {
        //     tmp[pl] = strategy.block_id[2][++ps];
        //     tmp[pr] = strategy.block_id[2][++ps];
        //     pl--;
        //     pr++;
        // }
        // assert(pl == 0);
        // assert(pr == strategy.block_id[2].size());
        // strategy.block_id[2] = tmp;
        // std::shuffle(strategy.block_id[2].begin() + 1, strategy.block_id[2].end(), rng);
    }

    return strategies;
}

HeadStrategy head_strategy(int disk_id) {
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
        if (global::get_request_number(disk_id, head) != 0 && global::timestamp - global::disks[disk_id].query[head] <= 70) {
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
        head = (head + 1) % global::V;
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
            if (global::get_request_number(disk_id, head) != 0 && global::timestamp - global::disks[disk_id].query[head] <= 70) {
                strategy.add_action(HeadActionType::JUMP, head);
                break;
            }
        }
    }

    return strategy;
}
}  // namespace baseline