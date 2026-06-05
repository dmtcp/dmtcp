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
#include <limits>
#include <list>
#include <map>
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
    using size_type = size_t;
    using difference_type = ptrdiff_t;
    using pointer = T *;
    using const_pointer = const T *;
    using reference = T&;
    using const_reference = const T&;
    using value_type = T;
    using propagate_on_container_move_assignment = std::true_type;

  public:
    // Constructors
    DmtcpAlloc() noexcept = default;

    DmtcpAlloc(const DmtcpAlloc&) noexcept = default;

    template<typename U>
    DmtcpAlloc(const DmtcpAlloc<U>&) noexcept {}

    DmtcpAlloc&operator=(const DmtcpAlloc&) noexcept = default;

    // Destructor
    ~DmtcpAlloc() noexcept = default;

    // Utility functions
    pointer address(reference r) const
    {
      return &r;
    }

    const_pointer address(const_reference c) const
    {
      return &c;
    }

    size_type max_size() const
    {
      return std::numeric_limits<size_t>::max() / sizeof(T);
    }

    // Rebind to allocators of other types
    template<typename U>
    struct rebind {
      using other = DmtcpAlloc<U>;
    };

    // Allocate raw memory
    pointer allocate(size_type n)
    {
      // void* p = malloc( n * sizeof(T) );
      // if( p == NULL )
      // throw std::bad_alloc();
      // return pointer(p);
      return pointer(jalib::JAllocDispatcher::malloc(n * sizeof(T)));
    }

    // Free raw memory.
    void deallocate(pointer p, size_type n)
    {
      //// assert( p != NULL );
      //// The standard states that p must not be NULL. However, some
      //// STL implementations fail this requirement, so the check must
      //// be made here.
      // if( p == NULL )
      // return;
      // free( p );
      jalib::JAllocDispatcher::free(p); //, n * sizeof(T));
    }

    //// Non-standard Dinkumware hack for Visual C++ 6.0 compiler.
    //// VC 6 doesn't support template rebind.
    // char* _Charalloc( size_type nBytes )
    // {
    // char* p = reinterpret_cast<char*>( malloc( nBytes ) );
    // if( p == NULL )
    // throw dmtcp::bad_alloc();
    // return p;
    // }
}; // end of DmtcpAlloc

// Comparison
template<typename T1, typename T2>
bool
operator==(const DmtcpAlloc<T1>&, const DmtcpAlloc<T2>&) throw()
{
  return true;
}

template<typename T1, typename T2>
bool
operator!=(const DmtcpAlloc<T1>&, const DmtcpAlloc<T2>&) throw()
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
