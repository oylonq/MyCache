#pragma once

namespace MyCache {

template <typename Key, typename Value> class ICachePolicy {
public:
  virtual ~ICachePolicy() {};

  // 添加缓存接口
  virtual void put(Key key, Value value) = 0;

  // key 是传入参数  访问到的值以传出参数的形式返回 / 访问成功返回 true
  virtual bool get(Key key, Value value) = 0;
  // 如果缓存中能找到 key，则直接返回 value
  virtual Value get(Key key) = 0;
};

} // namespace MyCache
