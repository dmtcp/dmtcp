/****************************************************************************
 *   Copyright (C) 2026 by DMTCP contributors                               *
 *                                                                          *
 *  This file is part of DMTCP.                                             *
 *                                                                          *
 *  DMTCP is free software: you can redistribute it and/or                  *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 ****************************************************************************/

#ifndef DMTCP_JSON_H
#define DMTCP_JSON_H

#include <stdint.h>

#include <cstdio>
#include <string_view>
#include <type_traits>

#include "dmtcpalloc.h"

namespace dmtcp
{

template<typename T>
struct UnsupportedJsonValue {
  static const bool value = false;
};

class Json
{
  public:
    Json();

    template<typename T>
    void appendField(std::string_view key, T value);

    string str() const;

  private:
    string _json;

    template<typename T>
    void append(T value);

    template<typename T>
      requires std::is_integral_v<std::remove_cv_t<T>> &&
               std::is_signed_v<std::remove_cv_t<T>> &&
               (!std::is_same_v<std::remove_cv_t<T>, bool>)
    void append(T value);

    template<typename T>
      requires std::is_integral_v<std::remove_cv_t<T>> &&
               std::is_unsigned_v<std::remove_cv_t<T>> &&
               (!std::is_same_v<std::remove_cv_t<T>, bool>)
    void append(T value);

    void append(bool value);
    void append(char *value);
    void append(const char *value);
    void append(std::string_view value);
    void appendFieldName(std::string_view key);
    void appendString(std::string_view value);
};

template<typename T>
void
Json::appendField(std::string_view key, T value)
{
  appendFieldName(key);
  append(value);
}

template<typename T>
void
Json::append(T)
{
  static_assert(UnsupportedJsonValue<T>::value,
                "unsupported JSON value type");
}

template<typename T>
  requires std::is_integral_v<std::remove_cv_t<T>> &&
           std::is_signed_v<std::remove_cv_t<T>> &&
           (!std::is_same_v<std::remove_cv_t<T>, bool>)
void
Json::append(T value)
{
  char buf[32];
  snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(value));
  _json += buf;
}

template<typename T>
  requires std::is_integral_v<std::remove_cv_t<T>> &&
           std::is_unsigned_v<std::remove_cv_t<T>> &&
           (!std::is_same_v<std::remove_cv_t<T>, bool>)
void
Json::append(T value)
{
  char buf[32];
  snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(value));
  _json += buf;
}
} // namespace dmtcp

#endif // ifndef DMTCP_JSON_H
