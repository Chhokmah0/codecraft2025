#include <algorithm>
#include <cassert>
#include <numeric>
#include <random>

#include "global.hpp"
#include "structures.hpp"

namespace baseline {

int target_disk_id = 1;
std::vector<int> local_disk_empty_block_nums;
std::vector<int> local_disk_p;
std::mt19937 rng(0);

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

std::vector<ObjectWriteStrategy> write_strategy(const std::vector<ObjectWriteRequest>& objects) {
    std::vector<ObjectWriteStrategy> strategies(objects.size());
    local_disk_empty_block_nums.clear();
    local_disk_empty_block_nums.push_back(0);
    local_disk_p.clear();
    local_disk_p.push_back(1);
    for (int i = 1; i <= global::N; i++) {
        local_disk_empty_block_nums.push_back(global::disks[i].empty_block_num);
        local_disk_p.push_back(global::disks[i].head);
    }

    std::vector<int> object_index(objects.size());
    std::iota(object_index.begin(), object_index.end(), 0);
    // 将具有相同 tag 的元素放在一起。
    std::sort(object_index.begin(), object_index.end(), [&](int i, int j) { return objects[i].tag < objects[j].tag; });

    for (int i = 0; i < objects.size(); i++) {
        const ObjectWriteRequest& object = objects[object_index[i]];
        ObjectWriteStrategy& strategy = strategies[object_index[i]];

        strategy.object = object;

        // 选定硬盘
        for (int j = 0; j < 3; j++) {
            while (local_disk_empty_block_nums[target_disk_id] < object.size) {
                target_disk_id = target_disk_id % global::N + 1;
            }
            strategy.disk_id[j] = target_disk_id;
            target_disk_id = target_disk_id % global::N + 1;
            local_disk_empty_block_nums[strategy.disk_id[j]] -= object.size;

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
        std::shuffle(strategy.block_id[2].begin() + 1, strategy.block_id[2].end(), rng);
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
            if (global::get_request_number(disk_id, head) != 0) {
                strategy.add_action(HeadActionType::JUMP, head);
                break;
            }
        }
        // 如果没有需要读取的块，则原地不动
    }

    return strategy;
}
}  // namespace baseline