///////////////////////////////////////////////////////////////////////////////

//
// BASED ON:
//
// DmtcpAlloc.h
//
// Malloc-based allocator. Uses standard malloc and free.
//
// Copyright � 2002 Pete Isensee (PKIsensee@msn.com).
// All rights reserved worldwide.
//
// Permission to copy, modify, reproduce or redistribute this source code is
// granted provided the above copyright notice is retained in the resulting
// source code.
//
// This software is provided "as is" and without any express or implied
// warranties.
//
///////////////////////////////////////////////////////////////////////////////

#ifndef DMTCPALLOC_H
#define DMTCPALLOC_H

#include <cstddef>
#include <fstream>
#include <iostream>
#include <list>
#include <limits>
#include <map>
#include <new>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "../jalib/jalloc.h"

namespace dmtcp
{
template<typename T>
class DmtcpAlloc
{
  public:
    using value_type = T;
    using propagate_on_container_move_assignment = std::true_type;

  public:
    // Constructors
    // This allocator is stateless; defaulted special members have no custom
    // behavior.
    DmtcpAlloc() noexcept = default;

    DmtcpAlloc(const DmtcpAlloc&) noexcept = default;

    template<typename U>
    DmtcpAlloc(const DmtcpAlloc<U>&) noexcept {}

    DmtcpAlloc& operator=(const DmtcpAlloc&) noexcept = default;

    ~DmtcpAlloc() noexcept = default;

    // Allocate raw memory
    T *allocate(std::size_t n)
    {
      if (n > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
        throw std::bad_array_new_length();
      }
      return static_cast<T *>(jalib::JAllocDispatcher::malloc(n * sizeof(T)));
    }

    // Free raw memory.
    void deallocate(T *p, std::size_t)
    {
      jalib::JAllocDispatcher::free(p);
    }
}; // end of DmtcpAlloc

// Comparison
template<typename T1, typename T2>
bool
operator==(const DmtcpAlloc<T1>&, const DmtcpAlloc<T2>&) noexcept
{
  return true;
}

template<typename T1, typename T2>
bool
operator!=(const DmtcpAlloc<T1>&, const DmtcpAlloc<T2>&) noexcept
{
  return false;
}

using string =
  std::basic_string<char, std::char_traits<char>, DmtcpAlloc<char>>;
using stringstream =
  std::basic_stringstream<char, std::char_traits<char>, DmtcpAlloc<char>>;
using istringstream =
  std::basic_istringstream<char, std::char_traits<char>, DmtcpAlloc<char>>;
using ostringstream =
  std::basic_ostringstream<char, std::char_traits<char>, DmtcpAlloc<char>>;
using ostream = std::ostream;
using istream = std::istream;
using iostream = std::iostream;
using fstream = std::fstream;
using ofstream = std::ofstream;
using ifstream = std::ifstream;

template<typename T>
using vector = std::vector<T, DmtcpAlloc<T>>;

template<typename T>
using list = std::list<T, DmtcpAlloc<T>>;

template<typename K, typename V>
using map = std::map<K,
                     V,
                     std::less<K>,
                     DmtcpAlloc<std::pair<const K, V>>>;

template<typename K, typename V>
using unordered_map = std::unordered_map<K,
                                         V,
                                         std::hash<K>,
                                         std::equal_to<K>,
                                         DmtcpAlloc<std::pair<const K, V>>>;

template<typename K>
using set = std::set<K, std::less<K>, DmtcpAlloc<K>>;

template<typename K>
using unordered_set = std::unordered_set<K,
                                         std::hash<K>,
                                         std::equal_to<K>,
                                         DmtcpAlloc<K>>;
}

// custom specialization of std::hash can be injected in namespace std
template<>
struct std::hash<dmtcp::string>
{
    std::size_t operator()(dmtcp::string const& s) const noexcept
    {
        return std::_Hash_impl::hash(s.data(), s.length());
    }
};

#endif // ifndef DMTCPALLOC_H
