#ifndef CPPUNIT_PORTABILITY_FLOATINGPOINT_H_INCLUDED
#define CPPUNIT_PORTABILITY_FLOATINGPOINT_H_INCLUDED

#include <cppunit/Portability.h>
#include <cmath>

CPPUNIT_NS_BEGIN

inline bool floatingPointIsUnordered( double x )
{
    return std::isnan(x);
}


/// \brief Tests if a floating-point is finite.
/// @return \c true if x is neither a NaN, nor +inf, nor -inf, \c false otherwise.
inline int floatingPointIsFinite( double x )
{
    return std::isfinite(x);
}

CPPUNIT_NS_END

#endif // CPPUNIT_PORTABILITY_FLOATINGPOINT_H_INCLUDED
