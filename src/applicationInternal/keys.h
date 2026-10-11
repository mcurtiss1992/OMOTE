#pragma once

#include <map>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <initializer_list>
#include <utility>
#include <stdint.h>

enum repeatModes {
  // only as fallback
  REPEAT_MODE_UNKNOWN,
  // if you short press or hold a key on the keypad, only one single command from keyCommands_short is sent
  // -> best used if you do not want a command to be sent more than once, even if you press the key (too) long, e.g. when toggling power
  SHORT,
  // if you hold a key on the keypad, a command from keyCommands_short is sent repeatedly
  // -> best used e.g. for holding the key for "volume up"
  SHORT_REPEATED,
  // if you short press a key, a command from keyCommands_short is sent once.
  // if you hold a key on the keypad, a command from keyCommands_long is sent (no command from keyCommands_short before)
  // -> best used if a long key press should send a different command than a short press
  SHORTorLONG,
};

// Key -> value table for the key mappings of scenes and GUIs. Works like the std::map it replaces (count, at,
// operator[], initializer lists), but keeps its entries in one sorted array: a std::map node costs ~40 bytes of
// heap per key, this 2-4 bytes. With ~170 mapped keys that is several KB of the ESP32's small heap.
template <typename V>
class KeyMap {
public:
  typedef std::pair<char, V> value_type;
  KeyMap() {}
  KeyMap(std::initializer_list<value_type> list) {assign(list);}
  KeyMap& operator=(std::initializer_list<value_type> list) {assign(list); return *this;}

  size_t count(char key) const {return find(key) != entries.end() ? 1 : 0;}
  const V& at(char key) const {
    auto it = find(key);
    if (it == entries.end()) {throw std::out_of_range("KeyMap::at");}
    return it->second;
  }
  V& operator[](char key) {
    auto it = std::lower_bound(entries.begin(), entries.end(), key, less);
    if ((it == entries.end()) || (it->first != key)) {
      it = entries.insert(it, value_type(key, V()));
    }
    return it->second;
  }
  size_t size() const {return entries.size();}
  bool empty() const {return entries.empty();}
  void clear() {entries.clear(); entries.shrink_to_fit();}
  typename std::vector<value_type>::const_iterator begin() const {return entries.begin();}
  typename std::vector<value_type>::const_iterator end() const {return entries.end();}

private:
  static bool less(const value_type& entry, char key) {return entry.first < key;}
  typename std::vector<value_type>::const_iterator find(char key) const {
    auto it = std::lower_bound(entries.begin(), entries.end(), key, less);
    return ((it != entries.end()) && (it->first == key)) ? it : entries.end();
  }
  void assign(std::initializer_list<value_type> list) {
    entries.clear();
    for (const value_type& entry : list) {(*this)[entry.first] = entry.second;}
    entries.shrink_to_fit();
  }
  std::vector<value_type> entries;
};

typedef KeyMap<repeatModes> *key_repeatModes;
typedef KeyMap<uint16_t> *key_commands_short;
typedef KeyMap<uint16_t> *key_commands_long;

void keypad_loop(void);
