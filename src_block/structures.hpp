#pragma once

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <map>
#include <ostream>
#include <set>
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
    int disk_id[3];                // 三个副本的目标硬盘
    std::vector<int> block_id[3];  // 三个副本的每个块在目标硬盘上的块号，注意 object 的块和硬盘上的块都是从 1
                                   // 开始编号的，block_id[0] 不使用。
};

// 磁头动作
enum class HeadActionType {
    JUMP,  // 跳转
    READ,  // 读取
    PASS,  // 跳过
};

struct HeadAction {
    HeadActionType type;
    int target;  // 跳转的目标，从 1 开始编号
};

struct HeadStrategy {
    std::vector<HeadAction> actions;

    void add_action(HeadActionType type, int target = 0) { actions.push_back(HeadAction{type, target}); }

    friend std::ostream& operator<<(std::ostream& out, const HeadStrategy& strategy) {
        for (const auto& action : strategy.actions) {
            switch (action.type) {
                case HeadActionType::JUMP:
                    out << "j " + std::to_string(action.target);
                    return out;
                case HeadActionType::READ:
                    out << 'r';
                    break;
                case HeadActionType::PASS:
                    out << 'p';
                    break;
            }
        }
        out << '#';
        return out;
    }
};

struct ObjectBlock {
    int object_id;  // 为 0 时表示这里没有对象
    int block_index;
};

struct Range {
    int l;
    int r;

    bool operator<(const Range& rth) const {
        return l < rth.l;
    }
};

struct Disk {
    HeadActionType pre_action;
    int pre_action_cost;

    int head;
    std::vector<ObjectBlock> blocks;                 // 从 1 开始编号，0 号块不使用
    std::vector<std::pair<int, int>> div_blocks_id;  // 分成若干个块,(start,id)
    std::vector<int> used_id;                        // 每个块中不同元素的放置情况，用2进制数表示
    std::vector<int> empty_block_size;               // 每个块中空余大小
    std::vector<std::vector<int>> used_id_cnt;       // 每个块里面，用到的种类的个数
    int empty_block_num;
    int part = 2048;               // 每x长度分一个块
    std::set<Range> empty_range;  // 空余的连续块
    std::set<int> used;           // 被使用的块
    std::map<int, int> query;     // 拥有查询的块，(index, last_query_time)

    Disk(int v)
        : pre_action(HeadActionType::JUMP), pre_action_cost(64 / 4 * 5), head(1), blocks(v + 1), empty_block_num(v) {
        empty_range.insert(Range{1, v});
        int cnt = 0;
        empty_block_size.push_back(0);
        for (int i = 1; i <= v; i += part) {
            div_blocks_id.push_back({i, ++cnt});
            empty_block_size.push_back(std::min(part, v - i + 1));
        }
        used_id.resize(cnt + 1, 0);
        used_id_cnt.resize(cnt + 1, std::vector<int>(17, 0));
    }

    void use(int index, int tag) {
        used.insert(index);
        empty_block_size[(index + part - 1) / part]--;
        used_id_cnt[(index + part - 1) / part][tag]++;
        if (used_id_cnt[(index + part - 1) / part][tag] == 1) {
            used_id[(index + part - 1) / part] += 1 << tag;
        }
        auto it = empty_range.upper_bound(Range{index, INT32_MAX});
        it--;
        Range range = *it;
        empty_range.erase(it);
        if (range.l <= index - 1) {
            empty_range.insert(Range{range.l, index - 1});
        }
        if (index + 1 <= range.r) {
            empty_range.insert(Range{index + 1, range.r});
        }
    }

    void unuse(int index, int tag) {
        used.erase(index);
        empty_block_size[(index + part - 1) / part]++;
        used_id_cnt[(index + part - 1) / part][tag]--;
        if (used_id_cnt[(index + part - 1) / part][tag] == 0) {
            used_id[(index + part - 1) / part] -= 1 << tag;
        }
        auto it = empty_range.lower_bound(Range{index, 0});
        int r = index;
        if (it != empty_range.end() && it->l == index + 1) {
            r = it->r;
        }
        int l = index;
        if (it != empty_range.begin() && std::prev(it)->r == index - 1) {
            l = std::prev(it)->l;
        }
        if (l != index) {
            empty_range.erase(std::prev(it));
        }
        if (r != index) {
            empty_range.erase(it);
        }

        empty_range.insert(Range{l, r});
    }
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
    int disk_id[3];                // 三个副本的目标硬盘
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
