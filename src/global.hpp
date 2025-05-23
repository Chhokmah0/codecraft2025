#pragma once
#include <climits>
#include <random>
#include <unordered_map>

#include "structures.hpp"

namespace global {

inline int T;  // 有 T 个时间片存在读取、写入、删除操作，本次数据有 T+105 个时间片
inline int M;  // 对象标签的数量
inline int N;  // 存储系统中硬盘的个数
inline int V;  // 每个硬盘中存储单元的个树
inline int G;  // 每个磁头每个时间片最多消耗的令牌数
inline int K;  // 垃圾回收时最多可以交换 block 的次数

inline std::mt19937_64 rng(0);  // 全局随机器

// fre_xxx[i][j] 表示相应时间片内对象标签为 i 的读取、写入、删除操作的对象大小之和。
// i 和 j 从 1 开始编号
inline std::vector<std::vector<int>> fre_del, fre_write, fre_read;
inline std::vector<int> g;
inline int fre_len;

inline int timestamp;                          // 全局时间戳
inline std::vector<Disk> disks;                // 从 1 开始编号
inline HashTable<int, Object> objects;         // (object_id, Object)
inline HashTable<int, int> request_object_id;  // (req_id, object_id)
}  // namespace global