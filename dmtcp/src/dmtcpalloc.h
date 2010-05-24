///////////////////////////////////////////////////////////////////////////////
//
// BASED ON:
//
//  DmtcpAlloc.h
//
//  Malloc-based allocator. Uses standard malloc and free.
//
//  Copyright © 2002 Pete Isensee (PKIsensee@msn.com).
//  All rights reserved worldwide.
//
//  Permission to copy, modify, reproduce or redistribute this source code is
//  granted provided the above copyright notice is retained in the resulting 
//  source code.
// 
//  This software is provided "as is" and without any express or implied
//  warranties.
//
///////////////////////////////////////////////////////////////////////////////

#ifndef DMTCPALLOC_H
#define DMTCPALLOC_H

#include "../jalib/jalloc.h"
#include <memory>
#include <limits>
#include <stdlib.h>
#include <vector>
#include <string>
#include <map>
#include <list>
#include <set>
#include <iostream>
#include <sstream>
#include <fstream>

#define DMTCPSTRING    dmtcp::string
#define DMTCPVECTOR(T) dmtcp::vector<T>
#define DMTCPLIST(T)   dmtcp::list<T>
#define DMTCPMAP(K, V) dmtcp::map<K, V>
#define DMTCPSET(K)    dmtcp::set<K>

namespace dmtcp 
{


template <typename T>
class DmtcpAlloc
{
public:
    typedef size_t    size_type;
    typedef ptrdiff_t difference_type;
    typedef T*        pointer;
    typedef const T*  const_pointer;
    typedef T&        reference;
    typedef const T&  const_reference;
    typedef T         value_type;

public:

    // Constructors
    DmtcpAlloc() throw() {}

    DmtcpAlloc( const DmtcpAlloc& ) throw() {}

    template <typename U>
    DmtcpAlloc( const DmtcpAlloc<U>& ) throw() {}

    DmtcpAlloc& operator=( const DmtcpAlloc& )
    {
        return *this;
    }

    // Destructor
    ~DmtcpAlloc() throw()
    {
    }

    // Utility functions
    pointer address( reference r ) const
    {
        return &r;
    }

    const_pointer address( const_reference c ) const
    {
        return &c;
    }

    size_type max_size() const
    {
        return std::numeric_limits<size_t>::max() / sizeof(T);
    }

    // In-place construction
    void construct( pointer p, const_reference c )
    {
        // placement new operator
        new( reinterpret_cast<void*>(p) ) T(c);
    }

    // In-place destruction
    void destroy( pointer p )
    {
        // call destructor directly
        (p)->~T();
    }

    // Rebind to allocators of other types
    template <typename U>
    struct rebind
    {
        typedef DmtcpAlloc<U> other;
    };

    // Allocate raw memory
    pointer allocate( size_type n, const void* = NULL )
    {
      //void* p = malloc( n * sizeof(T) );
      //if( p == NULL )
      //    throw std::bad_alloc();
      //return pointer(p);
      return pointer(jalib::JAllocDispatcher::allocate(n*sizeof(T)));
    }

    // Free raw memory.
    // Note that C++ standard defines this function as
    // deallocate( pointer p, size_type). Because Visual C++ 6.0
    // compiler doesn't support template rebind, Dinkumware uses
    // void* hack.
    void deallocate( void* p, size_type n )
    {
      //// assert( p != NULL );
      //// The standard states that p must not be NULL. However, some
      //// STL implementations fail this requirement, so the check must
      //// be made here.
      //if( p == NULL )
      //    return;
      //free( p );
      jalib::JAllocDispatcher::deallocate(p, n*sizeof(T));
    }

 // // Non-standard Dinkumware hack for Visual C++ 6.0 compiler.
 // // VC 6 doesn't support template rebind.
 // char* _Charalloc( size_type nBytes )
 // {
 //     char* p = reinterpret_cast<char*>( malloc( nBytes ) );
 //     if( p == NULL )
 //         throw dmtcp::bad_alloc();
 //     return p;
 // }

}; // end of DmtcpAlloc

// Comparison
template <typename T1, typename T2>
bool operator==( const DmtcpAlloc<T1>&,
                 const DmtcpAlloc<T2>& ) throw()
{
    return true;
}

template <typename T1, typename T2>
bool operator!=( const DmtcpAlloc<T1>&,
                 const DmtcpAlloc<T2>& ) throw()
{
    return false;
}

typedef std::basic_string< char, std::char_traits<char>, DmtcpAlloc<char> > string;
typedef std::basic_stringstream< char, std::char_traits<char>, DmtcpAlloc<char> > stringstream;
typedef std::basic_istringstream< char, std::char_traits<char>, DmtcpAlloc<char> > istringstream;
typedef std::basic_ostringstream< char, std::char_traits<char>, DmtcpAlloc<char> > ostringstream;
typedef std::ostream ostream;
typedef std::istream istream;
typedef std::iostream iostream;
typedef std::fstream fstream;
typedef std::ofstream ofstream;
typedef std::ifstream ifstream;

template < typename T > class vector: public std::vector<T, dmtcp::DmtcpAlloc<T> > {
public:
  vector(size_t n, const T& v=T()) : std::vector<T, dmtcp::DmtcpAlloc<T> >(n, v) {}
  vector() : std::vector<T, dmtcp::DmtcpAlloc<T> >() {}
};
template < typename T > class list: public std::list<T, dmtcp::DmtcpAlloc<T> > {};
template < typename K, typename V > class map: public std::map<K, V, std::less<K>, dmtcp::DmtcpAlloc<std::pair<K, V> > > {};
template < typename K > class set: public std::set<K, std::less<K>, dmtcp::DmtcpAlloc<K> > {};

}
#endif 

