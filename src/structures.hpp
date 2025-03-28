#pragma once

#include <cassert>
#include <iterator>
#include <limits>
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

    bool is_used_disk(int disk_id) const {
        for (int i = 0; i < 3; i++) {
            if (this->disk_id[i] == disk_id) {
                return true;
            }
        }
        return false;
    }
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
    int object_id;           // 为 0 时表示这里没有对象
    int object_size;         // 这里存的是 object_id 物品的大小
    int object_tag;          // 这里存的是 object_id 物品的标签
    int object_block_index;  // 这里存的是 object_id 物品的第几个分块
};

struct Range {
    int l;
    int r;

    bool operator<(const Range& rth) const { return l < rth.l; }
};

class Disk {
   public:
    std::vector<ObjectBlock> blocks;        // 从 1 开始编号，0 号块不使用
    std::vector<double> margin_gain;        // 每个 block 上的剩余查询收益
    std::vector<double> start_margin_gain;  // 不考虑衰减的每个block上的查询收益
    double total_margin_gain;               // 总的查询收益
    int empty_block_num;                    // 空余的块数量

    int head;  // 磁头的位置
    int v;     // 磁盘的总大小
    HeadActionType pre_action;
    int pre_action_cost;

    int slice_size;  // 分成若干个大块（Slice），每个 slice 的大小为 slice_size
    int slice_num;
    std::vector<int> slice_id;                           // slice_id[block_index] 表示这个位置被分到第几个块
    std::vector<int> slice_tag;                          // slice 内存储的对象的 tag，用 2 进制表达，0 表示没有对象
    std::vector<int> slice_last_tag;                     // slice 中最新的 tag
    std::vector<std::vector<int>> slice_tag_writed_num;  // slice 中每个 tag 的块数量
    std::vector<int> slice_empty_block_num;              // 每个 slice 中空闲的块数量
    std::vector<int> slice_start,
        slice_end;                          // 第 i 个 slice 的范围为 [slice_start[i], slice_end[i]]，从 1 开始编号，0 号 slice 不使用
    std::vector<double> slice_margin_gain;  // 每个 slice 中的剩余查询收益

    std::vector<int> tag_slice_num;  // 每个 tag 在该硬盘上的 slice 数量

    std::set<Range> empty_range;         // 未被写入的连续块
    std::set<int> writed;                // 被写入的块
    std::map<int, int> last_query_time;  // 拥有查询的块，(index, last_query_time)

    Disk(int v, int m)
        : blocks(v + 1),
          margin_gain(v + 1),
          start_margin_gain(v + 1),
          total_margin_gain(0),
          empty_block_num(v),
          head(1),
          v(v),
          pre_action(HeadActionType::JUMP),
          pre_action_cost(0),
          slice_size((v + m - 1) / m),
          slice_num((v - 1) / slice_size + 1),
          slice_id(v + 1),
          slice_tag(slice_size + 1),
          slice_last_tag(slice_size + 1),
          slice_tag_writed_num(slice_size + 1, std::vector<int>(m + 1)),
          slice_empty_block_num(slice_size + 1),
          slice_start(slice_size + 1),
          slice_end(slice_size + 1),
          slice_margin_gain(slice_size + 1),
          tag_slice_num(m + 1) {
        empty_range.insert(Range{1, v});
        for (int i = 1; i <= v; i++) {
            slice_id[i] = (i - 1) / slice_size + 1;
            slice_empty_block_num[slice_id[i]]++;
            if (slice_start[slice_id[i]] == 0) {
                slice_start[slice_id[i]] = i;
            }
            slice_end[slice_id[i]] = i;
        }
    }

    Disk(int v, int m, int slice_size)
        : blocks(v + 1),
          margin_gain(v + 1),
          start_margin_gain(v + 1),
          total_margin_gain(0),
          empty_block_num(v),
          head(1),
          v(v),
          pre_action(HeadActionType::JUMP),
          pre_action_cost(0),
          slice_size(slice_size),
          slice_num((v - 1) / slice_size + 1),
          slice_id(v + 1),
          slice_tag(slice_size + 1),
          slice_last_tag(slice_size + 1),
          slice_tag_writed_num(slice_size + 1, std::vector<int>(m + 1)),
          slice_empty_block_num(slice_size + 1),
          slice_start(slice_size + 1),
          slice_end(slice_size + 1),
          slice_margin_gain(slice_size + 1),
          tag_slice_num(m + 1) {
        empty_range.insert(Range{1, v});
        for (int i = 1; i <= v; i++) {
            slice_id[i] = (i - 1) / slice_size + 1;
            slice_empty_block_num[slice_id[i]]++;
            if (slice_start[slice_id[i]] == 0) {
                slice_start[slice_id[i]] = i;
            }
            slice_end[slice_id[i]] = i;
        }
    }

    bool is_empty() const { return empty_block_num == v; }
    // NOTE: slice 不一定完全是空的，但是这里假设 slice 里此时物品很少
    bool is_slice_empty(int slice_id) const { return slice_last_tag[slice_id] == 0; }
    bool is_tag_empty(int tag) const { return tag_slice_num[tag] == 0; }

    // 在第 i 个块写入指定的物品
    void write(int index, ObjectBlock object) {
        blocks[index] = object;
        writed.insert(index);
        empty_block_num--;
        slice_empty_block_num[slice_id[index]]--;
        if (slice_tag_writed_num[slice_id[index]][object.object_tag] == 0) {
            slice_tag[slice_id[index]] |= 1 << object.object_tag;
            tag_slice_num[object.object_tag]++;
        }
        slice_last_tag[slice_id[index]] = object.object_tag;
        slice_tag_writed_num[slice_id[index]][object.object_tag]++;

        // 维护 empty_range
        auto it = empty_range.upper_bound(Range{index, std::numeric_limits<int>::max()});
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

    // 释放第 i 个块
    void erase(int index) {
        writed.erase(index);
        empty_block_num++;
        slice_empty_block_num[slice_id[index]]++;
        slice_tag_writed_num[slice_id[index]][blocks[index].object_tag]--;
        if (slice_tag_writed_num[slice_id[index]][blocks[index].object_tag] == 0) {
            slice_tag[slice_id[index]] &= ~(1 << blocks[index].object_tag);
            tag_slice_num[blocks[index].object_tag]--;
            if (slice_last_tag[slice_id[index]] == blocks[index].object_tag) {
                // 虽然不一定是空的，但是此时认为这个 slice 已经空了
                slice_last_tag[slice_id[index]] = 0;
            }
        }
        blocks[index] = ObjectBlock{0, 0, 0, 0};

        // 维护 empty_range
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

        // 释放查询
        last_query_time.erase(index);
        slice_margin_gain[slice_id[index]] -= margin_gain[index];
        total_margin_gain -= margin_gain[index];
        start_margin_gain[index] = 0;
        margin_gain[index] = 0;
    }

    void query(int index, int timestamp) {
        const ObjectBlock& object = blocks[index];
        assert(object.object_id != 0);
        last_query_time[index] = timestamp;
        double gain = (double)(object.object_size + 1) / object.object_size;
        slice_margin_gain[slice_id[index]] += gain;
        margin_gain[index] += gain;
        start_margin_gain[index] += gain;
        total_margin_gain += gain;
    }
    // 更新全局贡献
    void update(int index) {
        const ObjectBlock& object = blocks[index];
        assert(object.object_id != 0);
        slice_margin_gain[slice_id[index]] -= margin_gain[index];
        total_margin_gain -= margin_gain[index];
        margin_gain[index] = 0.999 * margin_gain[index];
        // margin_gain[index] += 0.01 * start_margin_gain[index];
        slice_margin_gain[slice_id[index]] += margin_gain[index];
        total_margin_gain += margin_gain[index];
    }
    // 在其他块被占用后，更新当前块的贡献
    void clean_gain(int index) {
        const ObjectBlock& object = blocks[index];
        assert(object.object_id != 0);
        slice_margin_gain[slice_id[index]] -= margin_gain[index];
        total_margin_gain -= margin_gain[index];
        margin_gain[index] = 0;
        start_margin_gain[index] = 0;
    }
    // 读取第 i 个块
    // 因为块有可能是被其它硬盘读取的，所以 block_index 并不一定等于 head
    void read(int block_index) {
        const ObjectBlock& object = blocks[block_index];
        if (object.object_id == 0) {
            return;
        }
        last_query_time.erase(block_index);
        slice_margin_gain[slice_id[block_index]] -= margin_gain[block_index];
        total_margin_gain -= margin_gain[block_index];
        margin_gain[block_index] = 0;
        start_margin_gain[block_index] = 0;
    }
};

struct ObjectReadRequest {
    int req_id;
    int object_id;
};

struct ObjectReadStatus {
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
    std::unordered_map<int, ObjectReadStatus> read_requests;  // (req_id, ObjectReadRequest)
    std::vector<int> request_number;                          // 第 i 个分块上的未完成请求数量

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
        read_requests[req_id] = ObjectReadStatus{req_id, 0, std::vector<char>(size + 1)};
        for (int i = 1; i <= size; i++) {
            request_number[i]++;
        }
    }

    // 获取这个对象第 block_index 个块的未完成请求数量，block_num 从 1 开始编号
    // 返回 0 说明没有请求
    int get_request_number(int block_index) const { return request_number[block_index]; }

    // 读取这个对象第 block_index 个块，返回所有被完成的读取请求的编号
    std::vector<int> read(int block_index) {
        std::vector<int> completed_requests;
        for (auto& [req_id, request] : read_requests) {
            if (request.readed[block_index] == false) {
                request.readed[block_index] = true;
                request.readed_size++;
                request_number[block_index]--;
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

    const std::unordered_map<int, ObjectReadStatus>& get_read_status() const { return read_requests; }
};
