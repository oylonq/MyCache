#pragma once

#include <cmath>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "ICachePolicy.h"

namespace MyCache {

template <typename Key, typename Value> class LruCache;

template <typename Key, typename Value> class LruNode {
private:
  Key key_;
  Value value_;
  std::size_t accessCount_;                 // 访问次数
  std::weak_ptr<LruNode<Key, Value>> prev_; // 用 weak_ptr 打破循环引用
  std::shared_ptr<LruNode<Key, Value>> next_;

public:
  LruNode(Key key, Value value) : key_(key), value_(value), accessCount_(1) {}

  // 提供必要的访问器
  Key getKey() const { return key_; }
  Value getValue() const { return value_; }
  Value setValue(const Value &value) { value_ = value; }
  size_t getAccessCount() const { return accessCount_; }
  void incrementAccessCount() { ++accessCount_; }

  friend class LruCache<Key, Value>;
};

template <typename Key, typename Value>
class LruCache : public MyCache::ICachePolicy<Key, Value> {
public:
  using LruNodeType = LruNode<Key, Value>;
  using NodePtr = std::shared_ptr<LruNodeType>;
  using NodeMap = std::pmr::unordered_map<Key, NodePtr>;

  LruCache(int capacity) : capacity_(capacity) { initializeList(); }

  ~LruCache() override = default;

  // 添加缓存
  void put(Key key, Value value) override {
    if (capacity_ <= 0)
      return;

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = nodeMap_.find(key);
    if (it != nodeMap_.end()) {
      updateExistingNode(it.second, value);
      return;
    }

    addNewNode(key, value);
  }

  bool get(Key key, Value value) override {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = nodeMap_.find(key);
    if (it != nodeMap_.end()) {
      moveToMostRecent(it->second);
      value = it->second->getValue();
      return true;
    }
    return false;
  }

  Value get(Key key) override {
    Value value{};
    // memset(&value, 0, sizeof(value));
    // memset是按字节设置内存的，对于复杂类型（如 string）使用 memset
    // 可能会破坏对象的内部结构
    get(key, value);
    get(key, value);
    return value;
  }

  // 删除指定元素
  void remove(Key key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = nodeMap_.find(key);
    if (it != nodeMap_.end()) {
      removeNode(it->second);
      nodeMap_.erase(it);
    }
  }

private:
  void initializeList() {
    // 创建首尾虚拟节点
    dummyHead_ = std::make_shared<LruNodeType>(Key(), Value());
    dummyTail_ = std::make_shared<LruNodeType>(Key(), Value());
    dummyHead_->next_ = dummyTail_;
    dummyTail_->prev_ = dummyHead_;
  }

  void updateExistingNode(NodePtr node, const Value &value) {
    node->setValue(value);
    moveToMostRecent(node);
  }

  void addNewNode(const Key &key, const Value &value) {
    if (nodeMap_.size() >= capacity_) {
      evictLeastRecent();
    }
    NodePtr newNode = std::make_shared<LruNodeType>(key, value);
    insertNode(newNode);
    nodeMap_[key] = newNode;
  }

  // 将节点移动到最新的位置
  void moveToMostRecent(NodePtr node) {
    removeNode(node);
    insertNode(node);
  }

  void removeNode(NodePtr node) {
    if (!node->prev_.expired() && node->next_) {
      auto prev = node->prev_.lock();
      prev->next_ = node->next_;
      node->next_->prev_ = prev;
      node->next_ = nullptr;
    }
  }

  void insertNode(NodePtr node) {

    node->next_ = dummyTail_;
    node->prev_ = dummyTail_->prev_;
    dummyTail_->prev_.lock()->next_ = node;
    dummyTail_->prev_ = node;
  }

  void evictLeastRecent() {
    NodePtr leastRecent = dummyHead_->next_;
    removeNode(leastRecent);
    nodeMap_.erase(leastRecent->getKey());
  }

private:
  int capacity_; // 缓存容量
  NodeMap nodeMap_;
  std::mutex mutex_;
  NodePtr dummyHead_; // 虚拟头节点
  NodePtr dummyTail_;
};

// LRU 优化：Lru-k版本。通过继承的方式进行再优化。

template <typename Key, typename Value>
class LruKCache : public LruCache<Key, Value> {
public:
  LruKCache(int capacity, int historyCapacity, int k)
      : LruCache<Key, Value>(capacity),
        historyList_(std::make_unique<LruKCache<Key, size_t>>(historyCapacity)),
        k_(k) {}

  Value get(Key key) {
    // 首先尝试从主缓存获取数据
    Value value{};
    bool inMainCache = LruCache<Key, Value>::get(key, value);

    // 获取并更新访问历史记录
    size_t historyListCount = historyList_->get(key);
    historyListCount++;
    historyList_->put(key, historyListCount);

    // 如果数据在主缓存里直接返回
    if (inMainCache) {
      return value;
    }

    // 如果数据不在主缓存里，但是访问次数达到了k次
    if (historyListCount >= k_) {
      // 检查是否有历史值记录
      auto it = historyValueMap_.find(key);
      if (it != historyValueMap_.end()) {
        // 有历史值将其添加到主缓存
        Value storedValue = it->second;

        // 从历史记录移除
        historyList_->remove(key);
        historyValueMap_.erase(it);

        // 添加到主缓存
        LruCache<Key, Value>::put(key, storedValue);

        return storedValue;
      }

      // 没有历史记录，无法添加到缓存，返回默认值
    }

    // 数据不在主缓存且不满足添加条件，返回默认值
    return value;
  }

  void put(Key key, Value value) {
    // 检查是否已在主缓存
    Value existingValue{};
    bool inMainCache = LruCache<Key, Value>::get(key, existingValue);

    if (inMainCache) {
      // 已在主缓存，直接更新
      LruCache<Key, Value>::put(key, value);
      return;
    }

    // 获取并更新访问历史
    size_t historyListCount = historyList_->get(key);
    historyListCount++;
    historyList_->put(key, historyListCount);

    // 保存值到历史记录映射，供后续get操作使用
    historyValueMap_[key] = value;

    // 检查是否达到k次访问阈值
    if (historyListCount >= k_) {
      // 达到阈值，添加到主缓存
      historyList_->remove(key);
      historyValueMap_.erase(key);
      LruCache<Key, Value>::put(key, value);
    }
  }

private:
  int k_; // 进入缓存队列的评判标准
  std::unique_ptr<LruCache<Key, size_t>>
      historyList_; // 访问数据历史记录（value 为访问次数）
  std::pmr::unordered_map<Key, Value>
      historyValueMap_; // 存储未达到k次访问的数据值
};

// Lru 优化：对lru进行分片，提高高并发使用性能
template <typename Key, typename Value> class HashLruCaches {
public:
  HashLruCaches(size_t capacity, int sliceNum)
      : capacity_(capacity_),
        sliceNum_(sliceNum > 0 ? sliceNum
                               : std::thread::hardware_concurrency()) {
    size_t sliceSize = std::ceil(
        capacity / static_cast<double>(sliceNum_)); // 获取每个分片的大小
    for (int i = 0; i < sliceNum_; ++i) {
      lruSliceCaches_.emplace_back(new LruCache<Key, Value>(sliceSize));
    }
  }

  void put(Key key, Value value) {
    // 获取key的hash值，并计算出对应的分片索引
    size_t sliceIndex = Hash(key) % sliceNum_;
    lruSliceCaches_[sliceIndex]->put(key, value);
  }

  bool get(Key key, Value &value) {
    // 获取key的hash值，并计算出对应的分片索引
    size_t sliceIndex = Hash(key);
    lruSliceCaches_[sliceIndex]->get(key, value);
  }

  Value get(Key key) {
    Value value;
    memset(&value, 0, sizeof(value));
    get(key, value);
    return value;
  }

private:
  // 将key转换为对应的hash值
  size_t Hash(Key key) {
    std::hash<Key> hashFunc;
    return hashFunc(key);
  }

private:
  size_t capacity_; // 总容量
  int sliceNum_;    // 切片数量
  std::vector<std::unique_ptr<LruCache<Key, Value>>>
      lruSliceCaches_; // 切片LRU缓存
};

} // namespace MyCache
