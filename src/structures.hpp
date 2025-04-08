#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <deque>
#include <iterator>
#include <limits>
#include <ostream>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

constexpr double ALPHA = 0.999;
constexpr auto generate_gain_mult() {
    std::array<double, 120> GAIN_MULT = {};
    for (size_t i = 0; i < GAIN_MULT.size(); ++i) {
        GAIN_MULT[i] = (i == 0) ? 1 : ALPHA * GAIN_MULT[i - 1];
    }
    return GAIN_MULT;
}
constexpr auto generate_simluate_mult() {
    std::array<double, 120> SIMLUATE_MULT = {};
    for (size_t i = 0; i < SIMLUATE_MULT.size(); ++i) {
        SIMLUATE_MULT[i] = i <= 10 ? 0.005 : 0.01;
    }
    for (size_t i = 0; i < SIMLUATE_MULT.size(); ++i) {
        SIMLUATE_MULT[i] = (i == 0) ? 2 : SIMLUATE_MULT[i - 1] - SIMLUATE_MULT[i];
    }
    return SIMLUATE_MULT;
}
constexpr auto GAIN_MULT = generate_gain_mult();
constexpr auto SIMLUATE_MULT = generate_simluate_mult();

struct ObjectWriteRequest {
    int id;
    int size;
    int tag;
};

struct ObjectWriteStrategy {
    ObjectWriteRequest object;
    int disk_id[3];                // 三个副本的目标硬盘
    int slice_id[3];               // 三个副本的目标 slice
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

// 一个仍然存在的读取请求被分为两种状态
// 1. 正常的读取请求，在 object 和 disk 的各处状态中都会体现。
// 2. 被清空收益的请求（被某个 head 包揽的请求），object 和 disk 的收益相关的部分不会维护它。
// 但是，在 object 和 disk 的状态中仍然会体现这个请求的存在，可以作为一个正常的请求被读取。

struct ObjectReadRequest {
    int req_id;
    int object_id;
};

struct ObjectReadStatus {
    int req_id;
    int readed_size;
    std::vector<char> readed;  // 从 1 开始标号，0 号块不使用
};

struct ObjectReadTime {
    int req_id;
    int timestamp;
};

class Object {
   public:
    int id;
    int size;
    int tag;
    int disk_id[3];                                           // 三个副本的目标硬盘
    int slice_id[3];                                          // 三个副本的目标 slice
    std::vector<int> block_id[3];                             // 三个副本的每个块在目标硬盘上的块号，注意硬盘上的块号是从 1 开始编号的
    std::deque<ObjectReadTime> read_queue;                    // 读取请求的队列，存储的是请求的编号和时间戳
    std::unordered_map<int, ObjectReadStatus> read_requests;  // (req_id, ObjectReadRequest)
    std::vector<int> request_number;                          // 第 i 个分块上的未完成请求数量
    std::unordered_set<int> unclean_gain_requests;            // 被清空收益的请求
   public:
    Object() = default;
    Object(ObjectWriteStrategy strategy) {
        id = strategy.object.id;
        size = strategy.object.size;
        tag = strategy.object.tag;
        for (int i = 0; i < 3; i++) {
            disk_id[i] = strategy.disk_id[i];
            slice_id[i] = strategy.slice_id[i];
            block_id[i] = strategy.block_id[i];
        }
        request_number.resize(size + 1);
    }

    void add_request(int req_id, int timestamp) {
        read_requests[req_id] = ObjectReadStatus{req_id, 0, std::vector<char>(size + 1)};
        for (int i = 1; i <= size; i++) {
            request_number[i]++;
        }
        read_queue.push_back(ObjectReadTime{req_id, timestamp});
        unclean_gain_requests.insert(req_id);
    }

    // 读取这个对象第 block_index 个块，返回所有被完成的读取请求的编号，需要在外侧删除这些请求
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
        return completed_requests;
    }

    // 获取超时的请求，需要在外侧删除这些请求
    std::vector<int> get_timeout_requests(int timestamp, int rel_lim = 105) {
        std::vector<int> timeout_requests;
        while (!read_queue.empty() && read_queue.front().timestamp + rel_lim <= timestamp) {
            int req_id = read_queue.front().req_id;
            read_queue.pop_front();

            if (!read_requests.count(req_id)) {
                continue;
            }
            timeout_requests.push_back(req_id);
        }
        return timeout_requests;
    }

    void clean_gain() { unclean_gain_requests.clear(); }

    // 放弃某个请求
    void erase_request(int req_id) {
        assert(read_requests.count(req_id));
        read_requests.erase(req_id);
        if (unclean_gain_requests.count(req_id)) {
            unclean_gain_requests.erase(req_id);
        }
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

// NOTE: request_num 是以 block 为粒度的
// 而 gain 是以 slice 为粒度的
class Disk {
   public:
    // 该数据结构用于维护时间片，会被用来计算 gain
    // 允许被删除的请求是不存在的
    struct TimeStruct {
        int timestamp;                     // 时间片的编号
        std::unordered_set<int> requests;  // 这个时间片的请求
        size_t sum_read_size;              // 读取的大小之和
        size_t sum_read_count;             // 读取的次数之和

        void add_request(const Object& object, int req_id) {
            assert(requests.count(req_id) == 0);
            requests.insert(req_id);
            sum_read_size += object.size;
            sum_read_count++;
        }

        void remove_request(const Object& object, int req_id) {
            // 允许被删除的请求是不存在的
            if (requests.count(req_id) == 0) {
                return;
            }
            requests.erase(req_id);
            sum_read_size -= object.size;
            sum_read_count--;
        }

        void clear_requests() {
            timestamp = 0;
            requests.clear();
            sum_read_size = 0;
            sum_read_count = 0;
        }

        double get_gain(int cur_timestamp) const {
            return (double)SIMLUATE_MULT[cur_timestamp - timestamp] * (sum_read_size + sum_read_count);
        }
    };

    struct EmptyRanges {
        std::set<Range> ranges;

        EmptyRanges(int n) : ranges() { ranges.insert(Range{1, n}); }

        void write(int index) {
            auto it = ranges.upper_bound(Range{index, std::numeric_limits<int>::max()});
            it--;
            Range range = *it;
            ranges.erase(it);
            if (range.l <= index - 1) {
                ranges.insert(Range{range.l, index - 1});
            }
            if (index + 1 <= range.r) {
                ranges.insert(Range{index + 1, range.r});
            }
        }

        void erase(int index) {
            auto it = ranges.lower_bound(Range{index, 0});
            int r = index;
            if (it != ranges.end() && it->l == index + 1) {
                r = it->r;
            }
            int l = index;
            if (it != ranges.begin() && std::prev(it)->r == index - 1) {
                l = std::prev(it)->l;
            }
            if (l != index) {
                ranges.erase(std::prev(it));
            }
            if (r != index) {
                ranges.erase(it);
            }
            ranges.insert(Range{l, r});
        }
    };

   public:
    int disk_id;                      // 磁盘的编号
    int v;                            // 磁盘的总大小
    int cur_time;                     // 当前的时间片
    int predict_time;                 // 需要预处理后多少秒的数据
    std::vector<ObjectBlock> blocks;  // 从 1 开始编号，0 号块不使用
    int empty_block_num;              // 空余的块数量
    std::vector<int> request_num;     // 每个 block 上的查询个数
    int total_request_num;            // 总的查询个数

    int head[2];  // 磁头的位置
    HeadActionType pre_action[2];
    int pre_action_cost[2];

    int slice_size;                                      // 分成若干个大块（Slice），每个 slice 的大小为 slice_size
    int slice_num;                                       // slice 的数量
    std::vector<int> slice_id;                           // slice_id[block_index] 表示这个位置被分到第几个块
    std::vector<int> slice_start, slice_end;             // 第 i 个 slice 的范围为 [slice_start[i], slice_end[i]]
    std::vector<int> slice_empty_block_num;              // 每个 slice 中空闲的块数量
    std::vector<int> slice_tag;                          // slice 内存储的对象的 tag，用 2 进制表达，0 表示没有对象
    std::vector<int> slice_last_tag;                     // slice 中最后放入的物品的 tag
    std::vector<std::vector<int>> slice_tag_writed_num;  // slice 中每个 tag 的块数量
    std::vector<int> slice_request_num;                  // 每个 slice 中的查询个数

    std::vector<int> tag_slice_num;  // 每个 tag 在该硬盘上的 slice 数量

    // 每个 slice 中，每个时间片的请求，time_requests[0] 是最新的，time_requests[i] 是往前第 i 个时间片的请求
    std::vector<std::deque<TimeStruct>> slice_time_requests;
    std::unordered_map<int, int> request_time;  // (req_id, timestamp)

    EmptyRanges empty_ranges;  // 未被写入的连续块

    Disk(int disk_id, int v, int m, int predict_time = 105)
        : disk_id(disk_id),
          v(v),
          cur_time(0),
          predict_time(predict_time),
          blocks(v + 1),
          empty_block_num(v),
          request_num(v + 1),
          total_request_num(0),
          head{1, 1},
          pre_action{HeadActionType::JUMP, HeadActionType::JUMP},
          pre_action_cost{0, 0},
          slice_size(std::max(5, (v - 1) / m + 1)),
          slice_num((v - 1) / slice_size + 1),
          slice_id(v + 1),
          slice_start(slice_num + 1),
          slice_end(slice_num + 1),
          slice_empty_block_num(slice_num + 1),
          slice_tag(slice_num + 1),
          slice_last_tag(slice_num + 1),
          slice_tag_writed_num(slice_num + 1, std::vector<int>(m + 1)),
          slice_request_num(slice_num + 1),
          tag_slice_num(m + 1),
          slice_time_requests(slice_num + 1),
          request_time(),
          empty_ranges(v) {
        for (int i = 1; i <= v; i++) {
            slice_id[i] = (i - 1) / slice_size + 1;
            if (slice_start[slice_id[i]] == 0) {
                slice_start[slice_id[i]] = i;
            }
            slice_end[slice_id[i]] = i;
        }
        for (int i = 1; i <= slice_num; i++) {
            slice_empty_block_num[i] = slice_end[i] - slice_start[i] + 1;
            slice_time_requests[i].push_front(TimeStruct{0, {}, 0, 0});
        }
    }

    bool is_empty() const { return empty_block_num == v; }
    bool is_block_empty(int block_index) const { return blocks[block_index].object_id == 0; }
    bool is_slice_empty(int slice_id) const {
        return slice_empty_block_num[slice_id] == slice_end[slice_id] - slice_start[slice_id] + 1;
    }
    bool has_tag(int tag) const { return tag_slice_num[tag] != 0; }

    // 获取自己是 object 的第几个副本
    int get_copy_id(const Object& object) const {
        int copy_id = std::find(object.disk_id, object.disk_id + 3, disk_id) - object.disk_id;
        assert(copy_id < 3);
        return copy_id;
    }

    // 写入指定的物品
    void write(const Object& object) {
        int copy_id = get_copy_id(object);
        int slice_id = object.slice_id[copy_id];
        for (int i = 1; i <= object.size; i++) {
            int index = object.block_id[copy_id][i];
            assert(blocks[index].object_id == 0);
            blocks[index] = ObjectBlock{object.id, object.size, object.tag, i};
            // 维护 block
            empty_block_num--;
            slice_empty_block_num[slice_id]--;
            if (slice_tag_writed_num[slice_id][object.tag] == 0) {
                slice_tag[slice_id] |= 1 << object.tag;
                tag_slice_num[object.tag]++;
            }
            slice_last_tag[slice_id] = object.tag;
            slice_tag_writed_num[slice_id][object.tag]++;
            // 维护 gain
            empty_ranges.write(index);
        }
    }

    // 删除指定的物体
    void erase(const Object& object) {
        int copy_id = get_copy_id(object);
        int slice_id = object.slice_id[copy_id];

        // 释放查询
        for (const auto& [req_id, request] : object.read_requests) {
            erase_request(object, req_id);
        }

        // 释放块
        for (int i = 1; i <= object.size; i++) {
            int index = object.block_id[copy_id][i];
            assert(blocks[index].object_id == object.id);

            empty_block_num++;
            blocks[index] = ObjectBlock{0, 0, 0, 0};
            slice_empty_block_num[slice_id]++;
            slice_tag_writed_num[slice_id][object.tag]--;
            if (slice_tag_writed_num[slice_id][object.tag] == 0) {
                slice_tag[slice_id] &= ~(1 << object.tag);
                tag_slice_num[object.tag]--;
                if (slice_last_tag[slice_id] == object.tag) {
                    slice_last_tag[slice_id] = 0;
                }
            }
            empty_ranges.erase(index);
        }
    }

    void erase_request(const Object& object, int req_id) {
        assert(request_time.count(req_id));
        int copy_id = get_copy_id(object);
        int slice_id = object.slice_id[copy_id];

        // 维护 block
        const ObjectReadStatus& request = object.read_requests.at(req_id);
        for (int i = 1; i <= object.size; i++) {
            // 如果已经被读取 or object 中已经读取完毕这个请求
            if (request.readed[i] || object.read_requests.count(req_id) == 0) {
                continue;
            }
            int index = object.block_id[copy_id][i];
            assert(blocks[index].object_id == object.id);
            request_num[index]--;
            slice_request_num[slice_id]--;
            total_request_num--;
        }

        // 维护 gain
        int timestamp = request_time[req_id];
        request_time.erase(req_id);
        int passed_time = cur_time - timestamp;
        if (passed_time >= slice_time_requests[slice_id].size()) {
            return;
        }
        TimeStruct& time_struct = slice_time_requests[slice_id][passed_time];
        time_struct.remove_request(object, req_id);
    }

    // 查询指定物品
    void query(const Object& object, int req_id) {
        int copy_id = get_copy_id(object);
        int slice_id = object.slice_id[copy_id];
        for (int i = 1; i <= object.size; i++) {
            int index = object.block_id[copy_id][i];
            assert(blocks[index].object_id == object.id);
            request_num[index]++;
            slice_request_num[slice_id]++;
            total_request_num++;
        }
        request_time[req_id] = cur_time;
        slice_time_requests[slice_id].front().add_request(object, req_id);
    }

    // read 指定的 block
    void read(int block_index) {
        // 允许读取空块
        if (blocks[block_index].object_id == 0) {
            return;
        }
        // 清空这个位置的 request，Object 中会被标记为已经 read，因此不会二次读取
        total_request_num -= request_num[block_index];
        slice_request_num[slice_id[block_index]] -= request_num[block_index];
        request_num[block_index] = 0;
    }

    double get_slice_gain(int slice_id) {
        if (slice_request_num[slice_id] == 0) {
            return 0;
        }
        double gain = 0;
        for (int i = 0; i < (int)slice_time_requests[slice_id].size(); i++) {
            gain += slice_time_requests[slice_id][i].get_gain(cur_time);
        }
        return gain;
    }

    void clean_object_gain(Object& object) {
        int copy_id = get_copy_id(object);
        int slice_id = object.slice_id[copy_id];
        for (auto req_id : object.unclean_gain_requests) {
            if (request_time.count(req_id)) {
                int timestamp = request_time[req_id];
                int passed_time = cur_time - timestamp;
                if (passed_time > predict_time) {
                    continue;
                }
                TimeStruct& time_struct = slice_time_requests[slice_id][passed_time];
                time_struct.remove_request(object, req_id);
            }
        }
    }

    // 模拟到下一个时间片，返回超出 predict_time 的请求
    // 注意：这里不知道 req_id 对应的 Object，因此这些请求之后需要和 object 一起调用 give_up_request 从而维护磁盘的状态
    std::vector<int> next_time() {
        cur_time++;
        std::vector<int> timeout_requests;
        for (int i = 1; i <= slice_num; i++) {
            auto& time_requests = slice_time_requests[i];
            time_requests.push_front(TimeStruct{cur_time, {}, 0, 0});
            // XXX：检查这里的时间
            if (time_requests.size() > predict_time + 1) {
                timeout_requests.insert(timeout_requests.end(), time_requests.back().requests.begin(),
                                        time_requests.back().requests.end());
                time_requests.pop_back();
            }
        }
        return timeout_requests;
    }
};
