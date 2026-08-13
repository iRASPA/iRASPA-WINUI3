/********************************************************************************************************************
    Lightweight cache matching the QCache pointer-ownership API used by DX12 shaders.
 ********************************************************************************************************************/

#pragma once

#include <list>
#include <memory>
#include <unordered_map>

template <typename Key, typename T>
class RKCache
{
public:
  explicit RKCache(int maxCost = 100) : _maxCost(maxCost > 0 ? maxCost : 1) {}

  void clear()
  {
    _map.clear();
    _order.clear();
  }

  bool contains(const Key &key) const { return _map.find(key) != _map.end(); }

  T *object(const Key &key)
  {
    auto it = _map.find(key);
    if (it == _map.end())
      return nullptr;
    _order.splice(_order.begin(), _order, it->second.orderIt);
    return it->second.value.get();
  }

  void insert(const Key &key, T *value)
  {
    if (!value)
      return;
    remove(key);
    _order.push_front(key);
    _map.emplace(key, Node{std::unique_ptr<T>(value), _order.begin()});
    trim();
  }

  bool remove(const Key &key)
  {
    auto it = _map.find(key);
    if (it == _map.end())
      return false;
    _order.erase(it->second.orderIt);
    _map.erase(it);
    return true;
  }

  int maxCost() const { return _maxCost; }
  void setMaxCost(int c)
  {
    _maxCost = c > 0 ? c : 1;
    trim();
  }

private:
  struct Node
  {
    std::unique_ptr<T> value;
    typename std::list<Key>::iterator orderIt;
  };

  void trim()
  {
    while (static_cast<int>(_map.size()) > _maxCost && !_order.empty())
    {
      const Key &oldest = _order.back();
      _map.erase(oldest);
      _order.pop_back();
    }
  }

  int _maxCost{100};
  std::list<Key> _order;
  std::unordered_map<Key, Node> _map;
};
