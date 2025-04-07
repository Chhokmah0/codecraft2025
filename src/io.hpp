#pragma once
#include <array>
#include <cassert>
#include <iostream>
#include <vector>

#include "global.hpp"
#include "structures.hpp"

namespace io {
inline void init_input() {
    std::cin >> global::T >> global::M >> global::N >> global::V >> global::G >> global::K;

    global::fre_len = (global::T + 1799) / 1800;
    global::fre_del.resize(global::M + 1);
    for (int i = 1; i <= global::M; i++) {
        global::fre_del[i].resize(global::fre_len + 1);
        for (int j = 1; j <= global::fre_len; j++) {
            std::cin >> global::fre_del[i][j];
        }
    }
    global::fre_write.resize(global::M + 1);
    for (int i = 1; i <= global::M; i++) {
        global::fre_write[i].resize(global::fre_len + 1);
        for (int j = 1; j <= global::fre_len; j++) {
            std::cin >> global::fre_write[i][j];
        }
    }
    global::fre_read.resize(global::M + 1);
    for (int i = 1; i <= global::M; i++) {
        global::fre_read[i].resize(global::fre_len + 1);
        for (int j = 1; j <= global::fre_len; j++) {
            std::cin >> global::fre_read[i][j];
        }
    }
}

inline void init_output() {
    std::cout << "OK" << '\n';
    std::cout.flush();
}

inline void timestamp_align(int timestamp) {
    std::string event;
    int time;
    std::cin >> event >> time;
    assert(time == timestamp);
    std::cout << event << " " << timestamp << '\n';
    std::cout.flush();
}

inline std::vector<int> delete_object_input() {
    int n_delete;
    std::cin >> n_delete;
    std::vector<int> deleted_requests(n_delete);
    for (int i = 0; i < n_delete; i++) {
        std::cin >> deleted_requests[i];
    }
    return deleted_requests;
}

inline void delete_object_output(const std::vector<int>& deleted_requests) {
    std::cout << deleted_requests.size() << '\n';
    for (auto req_id : deleted_requests) {
        std::cout << req_id << '\n';
    }
    std::cout.flush();
}

inline std::vector<ObjectWriteRequest> write_object_input() {
    int n_write;
    std::cin >> n_write;
    std::vector<ObjectWriteRequest> write_objects(n_write);
    for (int i = 0; i < n_write; i++) {
        std::cin >> write_objects[i].id >> write_objects[i].size >> write_objects[i].tag;
    }
    return write_objects;
}

inline void write_object_output(const std::vector<ObjectWriteStrategy>& write_strategies) {
    for (auto& strategy : write_strategies) {
        std::cout << strategy.object.id << '\n';
        for (int i = 0; i < 3; i++) {
            std::cout << strategy.disk_id[i] << " ";
            for (int j = 1; j <= strategy.object.size; j++) {
                std::cout << strategy.block_id[i][j] << " \n"[j == strategy.object.size];
            }
        }
    }
    std::cout.flush();
}

inline std::vector<ObjectReadRequest> read_object_input() {
    int n_read;
    std::cin >> n_read;
    std::vector<ObjectReadRequest> read_objects(n_read);
    for (int i = 0; i < n_read; i++) {
        std::cin >> read_objects[i].req_id >> read_objects[i].object_id;
    }
    return read_objects;
}

inline void read_object_output(const std::vector<std::array<HeadStrategy, 2>>& head_strategies,
                               const std::vector<int>& completed_requests) {
    for (int i = 1; i <= global::N; i++) {
        for (int j = 0; j < 2; j++) {
            std::cout << head_strategies[i][j] << '\n';
        }
    }
    std::cout << completed_requests.size() << '\n';
    for (auto req_id : completed_requests) {
        std::cout << req_id << '\n';
    }
    std::cout.flush();
}

inline void busy_requests_output(const std::vector<int>& busy_requests) {
    std::cout << busy_requests.size() << '\n';
    for (auto req_id : busy_requests) {
        std::cout << req_id << '\n';
    }
    std::cout.flush();
}

inline void garbage_collection_input() {
    std::string str;
    std::cin >> str;
    std::cin >> str;
}

// TODO: 临时方案
inline void garbage_collection_output(const std::vector<std::vector<std::pair<int, int>>>& used_swap) {
    std::cout << "GARBAGE COLLECTION" << '\n';
    for (int i = 1; i <= global::N; i++) {
        std::cout << used_swap[i].size() << '\n';
        std::cerr << global::timestamp << " disk " << i << " " << used_swap[i].size() << '\n';
        for (auto [start_part, end_part] : used_swap[i]) {
            std::cout << start_part << " " << end_part << '\n';
        }
    }
    std::cerr.flush();
    std::cout.flush();
}

}  // namespace io