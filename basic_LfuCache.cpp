#include <unordered_map>
class LfuCache {
private:
  // 双向链表节点
  struct Node {
    int key;
    int value;
    int freq; // 为每个双向链表的节点增加 freq 值，用于记录其被访问频数
    Node *pre, *next;
    Node(int key, int value, int freq) {
      this->key = key;
      this->value = value;
      this->freq = freq;
      pre = nullptr;
      next = nullptr;
    }
  };

  // 双向链表
  struct FreqList {
    int freq; // 用于标识双向链表中节点的共同访问频数
    Node *L, *R;
    FreqList(int freq) {
      this->freq = freq;
      L = new Node(-1, -1, -1);
      R = new Node(-1, -1, -1);
      L->next = R;
      R->pre = L;
    }
  };

  int n; // 缓存空间大小
  /*
  整个缓存中的节点最小访问频数， LFU 中为每个频数构造一个双向链表，
  当缓存空间满了时，首先需要知道当前缓存中最小的频数是多少，再需要
  找到该最小频数下最久为使用的数据淘汰。想要在 O(1) 时间复杂度下完
  成上述操作，需要使用双向链表结构，还需动态记录维护缓存空间中最小
  访问频数 minFreq
  */
  int minFreq;
  std::unordered_map<int, Node *>
      hashNode; // 节点哈希表，用于快速获取数据中 key 对应的节点信息
  std::pmr::unordered_map<int, FreqList *>
      hashFreq; // 频数双向链表哈希表，
                // 为每个访问频数构造一个双向链表，并用哈希表联系二者关系

  void deleteFromList(Node *node) {
    Node *pre = node->pre;
    Node *next = node->next;
    pre->next = next;
    next->pre = pre;
  }

  void append(Node *node) {
    int freq = node->freq;
    if (hashFreq.find(freq) == hashFreq.end()) {
      hashFreq[freq] = new FreqList(freq);
    }

    FreqList *curList = hashFreq[freq];

    Node *pre = curList->R->pre;
    Node *next = curList->R;
    pre->next = node;
    node->next = next;
    next->pre = node;
    node->pre = pre;
  }

public:
  // Lfu 缓存构造函数
  LfuCache(int capacity) {
    n = capacity; // 初始化缓存空间
    minFreq = 0;  // 初始化最小访问频数为 0
  }

  // 访问缓存数据

  int get(int key) {
    if (hashNode.find(key) != hashNode.end()) {
      // 缓存中存在该 key

      Node *node =
          hashNode[key]; // 利用节点哈希表， O(1) 时间复杂度下定位到该节点
      // 每次 get 操作会将该节点访问频数 +1，所以需要将它从原来频数对应的
      // 双向链表中删除
      deleteFromList(node);
      node->freq++;
      /*
      下面这个操作是为了防止当前 node
      对应的是最小频数双向链表里的唯一节点，具体情况可分两种讨论 情况
      1：如果当前 node 对应的是最小频数双向链表里的唯一节点，那么在进行对其的
      get 操作后，它 的频数 freq++，原双向链表节点数目变为 0，则最小频数
      minFreq++，即执行这个 if 操作 情况 2：如果当前 node
      对应的不是最小频数双向链表里的唯一节点，那么无需更新 minFreq
      */

      if (hashFreq[minFreq]->L->next == hashFreq[minFreq]->R)
        minFreq++;
      append(node);       // 加入新的频数对应的双向链表;
      return node->value; // 返回该 key 对应的 value
                          // 首先需要知道当前缓存中最小的频数是多少
    } else {
      return -1;
    }
  }

  // 更新缓存数据
  void put(int key, int value) {
    if (n == 0)
      return; // 缓存空间为 0，不可以加入任何数据
    if (get(key) != -1) {
      // 缓存中已经存在该 key ，复用一个 get 操作，
      // 就可以完成该节点对应双向链表的更新
      hashNode[key]->value = value; // 把该节点在节点哈希表 hashNode 中更新
    } else {
      // 缓存中不存在该 key，需要把节点插入到缓存空间中
      if (hashNode.size() == n) {
        // 缓存空间已满
        Node *node =
            hashFreq[minFreq]->L->next; // 找到该最小频数下最久为使用的数据淘汰
        deleteFromList(node);           // 在双向链表中删除该节点
        hashNode.erase(node->key);      // 在节点哈希表中删除该节点
      }
      // 缓存空间满 或
      // 未满两种情况，均需把新节点加入缓存（双向链表和节点哈希表均需插入）
      Node *node = new Node(key, value, 1); // 构造新节点，它的节点频数为 1
      hashNode[key] = node;                 // 插入节点哈希表
      minFreq = 1;  // 新插入的节点频数为 1 ，故最小频数应当变为 1
      append(node); // 插入到频数为 1 ， 对应的双向链表中
    }
  }
};
