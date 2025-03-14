#pragma once

#include <algorithm>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct ObjectWriteRequest {
    int id;
    int size;
    int tag;
};

struct ObjectWriteStrategy {
    ObjectWriteRequest object;
    int disk_id[3];  // 三个副本的目标硬盘
    std::vector<int> block_id[3];  // 三个副本的每个块在目标硬盘上的块号，注意 object 的块和硬盘上的块都是从 1
                                   // 开始编号的，block_id[0] 不使用。
};

struct ObjectReadRequest {
    int req_id;
    int readed_size;
    std::vector<char> readed;  // 从 1 开始标号，0 号块不使用
};

class Object {
   public:
    int id;
    int size;
    int tag;
    int disk_id[3];  // 三个副本的目标硬盘
    std::vector<int> block_id[3];  // 三个副本的每个块在目标硬盘上的块号，注意硬盘上的块号是从 1 开始编号的
   private:
    std::unordered_map<int, ObjectReadRequest> read_requests;  // (req_id, ObjectReadRequest)
    std::vector<int> request_number;                           // 每个块的未完成请求数量

   public:
    Object() = default;
    Object(ObjectWriteStrategy strategy) {
        id = strategy.object.id;
        size = strategy.object.size;
        tag = strategy.object.tag;
        for (int i = 0; i < 3; i++) {
            disk_id[i] = strategy.disk_id[i];
            block_id[i] = strategy.block_id[i];
        }
        request_number.resize(size + 1);
    }

    void add_request(int req_id) {
        read_requests[req_id] = ObjectReadRequest{req_id, 0, std::vector<char>(size + 1)};
        for (int i = 1; i <= size; i++) {
            request_number[i]++;
        }
    }

    // 获取第 block_num 个块的未完成请求数量，block_num 从 1 开始编号
    // 返回 0 说明没有请求
    int get_request_number(int block_num) const { return request_number[block_num]; }

    // 读取第 block_num 个块，返回所有被完成的读取请求的编号
    std::vector<int> read(int block_num) {
        std::vector<int> completed_requests;
        for (auto& [req_id, request] : read_requests) {
            if (request.readed[block_num] == false) {
                request.readed[block_num] = true;
                request.readed_size++;
                request_number[block_num]--;
            }
            if (request.readed_size == size) {
                completed_requests.push_back(req_id);
            }
        }
        for (auto req_id : completed_requests) {
            read_requests.erase(req_id);
        }
        return completed_requests;
    }

    const std::unordered_map<int, ObjectReadRequest>& get_read_requests() const { return read_requests; }
};

// 磁头动作
enum class HeadActionType {
    JUMP,  // 跳转
    READ,  // 读取
    PASS,  // 跳过
};

struct HeadAction {
    HeadActionType type;
    int target;  // 跳转的目标或读取的目标，从 1 开始编号
};

struct HeadStrategy {
    std::vector<HeadAction> actions;

    std::string to_string() const {
        std::string result;
        for (const auto& action : actions) {
            switch (action.type) {
                case HeadActionType::JUMP:
                    return "j " + std::to_string(action.target);
                case HeadActionType::READ:
                    result += 'r';
                    break;
                case HeadActionType::PASS:
                    result += 'p';
                    break;
            }
        }
        result += '#';
        return result;
    }
};

struct ObjectBlock {
    int object_id;  // 为 0 时表示这里没有对象
    int block_id;
};

struct Disk {
    HeadActionType pre_action;
    int pre_action_cost;

    int head;
    std::vector<ObjectBlock> blocks;  // 从 1 开始编号，0 号块不使用

    Disk(int v) : pre_action(HeadActionType::JUMP), pre_action_cost(64 / 4 * 5), head(1), blocks(v + 1) {}
};
