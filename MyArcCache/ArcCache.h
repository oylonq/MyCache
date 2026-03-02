#pragma once

#include "../ICachePolicy.h"
#include "ArcLfuPart.h"
#include "ArcLruPart.h"
#include <cstddef>
#include <memory>

namespace MyCache {
template <typename Key, typename Value>
class ArcCache : public ICachePolicy<Key, Value> {
public:
  explicit ArcCache(size_t capacity = 10, size_t transfromThreshold = 2)
      : capacity_(capacity), transfromThreshold_(transfromThreshold),
        lruPart_(std::make_unique<ArcLruPart<Key, Value>>(capacity,
                                                          transfromThreshold)),
        lfuPart_(std::make_unique<ArcLfuPart<Key, Value>>(capacity,
                                                          transfromThreshold)) {
  }
  ~ArcCache() override = default;

  void put(Key key, Value value) override {
    checkGhostCache(key);
    // 检查lfu部分是否存在该键
    bool inLfu = lfuPart_->contain(key);
    // 更新lru部分缓存
    lruPart_->put(key, value);
    // 如果lfu部分存在该键，则更新lfu部分
    if (inLfu) {
      lfuPart_->put(key, value);
    }
  }

  bool get(Key key, Value &value) override {
    checkGhostCache(key);
    bool shouldTransform = false;
    if (lruPart_->get(key, value, shouldTransform)) {
      if (shouldTransform) {
        lfuPart_->put(key, value);
      }
      return true;
    }
    return lfuPart_->get(key, value);
  }

  Value get(Key key) override {
    Value value{};
    get(key, value);
    return value;
  }

private:
  bool checkGhostCache(Key key) {
    bool inGhost = false;
    if (lruPart_->chechGhost(key)) {
      if (lfuPart_->decreaseCapacity()) {
        lruPart_->incrementCapacity();
      }
      inGhost = true;
    } else if (lfuPart_->checkGhost(key)) {
      if (lruPart_->decreaseCapacity()) {
        lfuPart_->increaseCapacity();
      }
      inGhost = true;
    }
    return inGhost;
  }

private:
  size_t capacity_;
  size_t transfromThreshold_;
  std::unique_ptr<ArcLruPart<Key, Value>> lruPart_;
  std::unique_ptr<ArcLfuPart<Key, Value>> lfuPart_;
};
} // namespace MyCache
