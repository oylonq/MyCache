#include "ICachePolicy.h"
#include <memory>
#include <mutex>
#include <unordered_map>
template <typename Key, typename Value> class LfrCache;

template <typename Key, typename Value> class FreqList {
private:
  struct Node {
    int freq; // 访问频次
    Key key;
    Value value;
    std::weak_ptr<Node> pre; // 使用 weak_ptr 打破循环引用
    std::shared_ptr<Node> next;

    Node() : freq(1), next(nullptr) {}
    Node(Key key, Value value)
        : freq(1), key(key), value(value), next(nullptr) {}
  };

  using NodePtr = std::shared_ptr<Node>;

  int freq_;     // 访问频次
  NodePtr head_; // 假头节点
  NodePtr tail_; // 假尾节点

public:
  explicit FreqList(int n) : freq_(n) {
    head_ = std::make_shared<Node>();
    tail_ = std::make_shared<Node>();

    head_->next = tail_;
    tail_->pre = head_;
  }

  bool isEmpty() const { return head_->next == tail_; }

  void addNode(NodePtr node) {
    if (!node || !head_ || !tail_) {
      return;
    }
    node->pre = tail_->pre;
    node->next = tail_;
    tail_->pre.lock()->next = node; // 使用 lock() 获取 shared_ptr
    tail_->pre = node;
  }

  void removeNode(NodePtr node) {
    if (!node || !head_ || !tail_) {
      return;
    }
    if (node->pre.expired() || !node->next) {
      return;
    }

    auto pre = node->pre.lock(); // 使用 lock() 获取 shared_ptr
    pre->next = node->next;
    node->next->pre = pre;
    node->next = nullptr; // 确保显示置空 next 指针，彻底断开节点与链表的连接
  }

  NodePtr getFirstNode() const { return head_->next; }

  friend class LfrCache<Key, Value>;
};

template <typename Key, typename Value>
class LfuCache : public MyCache::ICachePolicy<Key, Value> {
public:
  using Node = typename FreqList<Key, Value>::Node;
  using NodePtr = std::shared_ptr<Node>;
  using NodeMap = std::unordered_map<Key, NodePtr>;

  LfuCache(int capacity, int maxAverageNum = 10)
      : capacity_(capacity), minFreq_(INT8_MAX), maxAverageNum_(maxAverageNum),
        curAverageNum_(0), curTotalNum_(0) {}

  ~LfuCache() override = default;

  void put(Key key, Value value) override {
    if (capacity_ == 0) {
      return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = nodeMap_.find(key);
    if (it != nodeMap_.end()) {
      // 重置其 value 值
      it->second->value = value;
      // 找到了直接调整就好，不用再去 get 中再找一遍，但其实影响不大
      getInternal(it->second, value);
      return;
    }

    putInternal(key, value);
  }
  // value 值为传出参数
  bool get(Key key, Value &value) override {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = nodeMap_.find(key);
    if (it != nodeMap_.end()) {
      getInternal(it.second, value);
      return true;
    }

    return false;
  }

  Value get(Key key) override {
    Value value;
    get(key, value);
    return value;
  }

private:
  void putInternal(Key key, Value value);       // 添加缓存
  void getInternal(NodePtr node, Value &value); // 获取缓存

  void kickOut(); // 移除缓存中的过期数据

  void removeFromFreqList(NodePtr node); // 从频率链表中移除节点
  void addToFreqList(NodePtr node);      // 添加到频率链表

  void addFreqNum();              // 增加平均访问频次
  void decreaseFreqNum(int num);  // 减少平均访问频次
  void handleOverMaxAverageNum(); // 处理当前平均访问频次超过上限的情况

  void updateMinFreq();

private:
  int capacity_;      // 缓存容量
  int minFreq_;       // 最小访问频次(用于找到最小访问频次节点)
  int maxAverageNum_; // 最大平均访问频次
  int curAverageNum_; // 当前平均访问频次
  int curTotalNum_;   // 当前访问所有缓存次数总数
  std::mutex mutex_;  // 互斥锁
  NodeMap nodeMap_;   // key 到 缓存节点的映射
  std::unordered_map<int, FreqList<Key, Value> *>
      freqToFreqList_; // 访问频次到该频次链表的映射
};
