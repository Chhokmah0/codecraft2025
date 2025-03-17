#include <numeric>

#include "global.hpp"
#include "structures.hpp"

namespace baseline {
int target_disk_id = 1;
std::vector<int> local_disk_empty_block_nums;
std::vector<int> local_disk_p;

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

    std::vector<int> object_ids(objects.size());
    std::iota(object_ids.begin(), object_ids.end(), 0);
    // 将具有相同 tag 的元素放在一起。
    std::sort(object_ids.begin(), object_ids.end(), [&](int i, int j) { return objects[i].tag < objects[j].tag; });

    for (int i = 0; i < objects.size(); i++) {
        const ObjectWriteRequest& object = objects[object_ids[i]];
        ObjectWriteStrategy& strategy = strategies[object_ids[i]];

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
    }

    return strategies;
}

HeadStrategy head_strategy(int i) {
    const Disk& disk = global::disks[i];
    int head = disk.head;
    HeadActionType pre_action = disk.pre_action;
    int pre_action_cost = disk.pre_action_cost;

    HeadStrategy strategy;
    // 查看下一个有请求的位置
    // TODO
    strategy.add_action(HeadActionType::JUMP, head);

    return strategy;
}
}  // namespace baseline