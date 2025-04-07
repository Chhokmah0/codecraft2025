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

inline int move_head(int disk_id, int slice_id, int p, int step) {
    return mod(p, global::disks[disk_id].slice_start[slice_id], global::disks[disk_id].slice_end[slice_id], step);
}

inline std::vector<int> deleted_requests;
inline std::vector<int> completed_requests;

inline void clean_gain_after_head(Disk& disk, int head) {
    for (int pos = head; pos <= disk.slice_end[disk.slice_id[head]]; pos++) {
        ObjectBlock& block = disk.blocks[pos];
        if (block.object_id == 0 || disk.request_num[pos] == 0) continue;
        Object& object = global::objects[block.object_id];
        // 如果该 object 在这个硬盘上的所有 block 都在 after_head 往后，清除（包揽）这个物品的查询贡献
        bool should_clean_object = true;
        int copy_id = disk.get_copy_id(object);
        for (int i = 1; i <= object.size; i++) {
            if (object.block_id[copy_id][i] < head) {
                should_clean_object = false;
                break;
            }
        }
        if (!should_clean_object) continue;
        // 清理 gain
        for (int i = 0; i < 3; i++) {
            Disk& disk = global::disks[object.disk_id[i]];
            disk.clean_object_gain(object);
        }
        object.clean_gain();
    }
}

inline void give_up_request(int req_id) {
    assert(global::request_object_id.count(req_id));
    int object_id = global::request_object_id[req_id];
    Object& object = global::objects[object_id];
    // 维护磁盘的状态
    for (int i = 0; i < 3; i++) {
        Disk& disk = global::disks[object.disk_id[i]];
        disk.erase_request(object, req_id);
    }
    // 维护对象的状态
    object.erase_request(req_id);
    // 维护全局的状态
    global::request_object_id.erase(req_id);
}

inline void delete_object(int object_id) {
    assert(global::objects.count(object_id));

    // 清理硬盘上的数据
    Object& object = global::objects[object_id];
    for (int i = 0; i < 3; i++) {
        Disk& disk = global::disks[object.disk_id[i]];
        disk.erase(object);
    }

    std::vector<int> temp_deleted_requests;
    for (const auto& [req_id, request] : object.read_requests) {
        global::request_object_id.erase(req_id);
        temp_deleted_requests.push_back(req_id);
    }
    deleted_requests.insert(deleted_requests.end(), temp_deleted_requests.begin(), temp_deleted_requests.end());

    global::objects.erase(object_id);
}

inline void write_object(const ObjectWriteStrategy& strategy) {
    global::objects[strategy.object.id] = Object(strategy);
    for (int i = 0; i < 3; i++) {
        Disk& disk = global::disks[strategy.disk_id[i]];
        disk.write(global::objects[strategy.object.id]);
    }
}

// 模拟单个磁头的动作
inline void simulate_head(Disk& disk, int head_id, const HeadStrategy& strategy) {
    for (const auto& action : strategy.actions) {
        switch (action.type) {
            case HeadActionType::JUMP: {
                disk.pre_action[head_id] = HeadActionType::JUMP;
                disk.pre_action_cost[head_id] = global::G;
                disk.head[head_id] = action.target;
                break;
            }
            case HeadActionType::READ: {
                auto cost = disk.pre_action[head_id] != HeadActionType::READ
                                ? 64
                                : std::max(16, (disk.pre_action_cost[head_id] * 4 + 4) / 5);
                disk.pre_action[head_id] = HeadActionType::READ;
                disk.pre_action_cost[head_id] = cost;

                // NOTE: 模拟读取操作的方法，同时维护对象和磁盘的状态
                ObjectBlock& block = disk.blocks[disk.head[head_id]];
                if (block.object_id == 0) {
                    // 读取了一个空块，该操作也是合法的，但是需要特殊处理
                    disk.head[head_id] = disk.head[head_id] % global::V + 1;
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
                for (int request_id : temp_completed_requests) {
                    for (int i = 0; i < 3; i++) {
                        Disk& t_disk = global::disks[object.disk_id[i]];
                        t_disk.erase_request(object, request_id);
                    }
                    object.erase_request(request_id);
                }
                disk.head[head_id] = mod(disk.head[head_id], 1, global::V, 1);
                break;
            }
            case HeadActionType::PASS: {
                disk.pre_action[head_id] = HeadActionType::PASS;
                disk.pre_action_cost[head_id] = 1;
                disk.head[head_id] = mod(disk.head[head_id], 1, global::V, 1);
                break;
            }
        }
    }
}

}  // namespace baseline