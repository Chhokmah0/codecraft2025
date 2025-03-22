#pragma once

#include <algorithm>
#include <cassert>
#include <vector>

#include "../global.hpp"
#include "../structures.hpp"

namespace baseline {

// 维护用的代码，应该是不需要修改的
// 有什么需要维护的信息先讨论一下，看看能不能加到 structures 里面

// 取模操作，让 p+step 在 [l, r] 之间
inline int mod(int p, int l, int r, int step) {
    int len = r - l + 1;
    return (p - l + step % len + len) % len + l;
}

inline std::vector<int> deleted_requests;
inline std::vector<int> completed_requests;

inline void delete_object(int object_id) {
    assert(global::objects.count(object_id));

    // 清理硬盘上的数据
    Object& object = global::objects[object_id];
    for (int i = 0; i < 3; i++) {
        Disk& disk = global::disks[object.disk_id[i]];
        for (int j = 1; j <= object.size; j++) {
            disk.erase(object.block_id[i][j]);
        }
    }

    std::vector<int> temp_deleted_requests;
    for (const auto& [req_id, request] : object.get_read_status()) {
        temp_deleted_requests.push_back(req_id);
    }
    deleted_requests.insert(deleted_requests.end(), temp_deleted_requests.begin(), temp_deleted_requests.end());

    global::objects.erase(object_id);
}

inline void write_object(const ObjectWriteStrategy& strategy) {
    for (int i = 0; i < 3; i++) {
        Disk& disk = global::disks[strategy.disk_id[i]];
        for (int j = 1; j <= strategy.object.size; j++) {
            disk.write(strategy.block_id[i][j],
                       ObjectBlock{strategy.object.id, strategy.object.size, strategy.object.tag, j});
        }
    }

    global::objects[strategy.object.id] = Object(strategy);
}

// 模拟单个磁头的动作
inline void simulate_head(Disk& disk, const HeadStrategy& strategy) {
    for (const auto& action : strategy.actions) {
        switch (action.type) {
            case HeadActionType::JUMP: {
                disk.pre_action = HeadActionType::JUMP;
                disk.pre_action_cost = global::G;
                disk.head = action.target;
                break;
            }
            case HeadActionType::READ: {
                auto cost =
                    disk.pre_action != HeadActionType::READ ? 64 : std::max(16, (disk.pre_action_cost * 4 + 4) / 5);
                disk.pre_action = HeadActionType::READ;
                disk.pre_action_cost = cost;

                // NOTE: 模拟读取操作的方法，同时维护对象和磁盘的状态
                ObjectBlock& block = disk.blocks[disk.head];
                if (block.object_id == 0) {
                    // 读取了一个空块，该操作也是合法的，但是需要特殊处理
                    disk.head = disk.head % global::V + 1;
                    break;
                }
                Object& object = global::objects[block.object_id];
                auto temp_completed_requests = object.read(block.object_block_index);
                completed_requests.insert(completed_requests.end(), temp_completed_requests.begin(),
                                          temp_completed_requests.end());
                for (int i = 0; i < 3; i++) {
                    Disk& t_disk = global::disks[object.disk_id[i]];
                    t_disk.read(object.block_id[i][block.object_block_index]);
                }

                disk.head = disk.head % global::V + 1;
                break;
            }
            case HeadActionType::PASS: {
                disk.pre_action = HeadActionType::PASS;
                disk.pre_action_cost = 1;
                disk.head = disk.head % global::V + 1;
                break;
            }
        }
    }
}

}  // namespace baseline
