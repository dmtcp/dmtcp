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

#include "json.h"

#include <cstdio>

namespace dmtcp
{
Json::Json()
  : _json("{")
{}

string
Json::str() const
{
  string result = _json;
  result += "}";
  return result;
}

void
Json::appendFieldName(std::string_view key)
{
  char last = _json[_json.length() - 1];
  if (last != '{' && last != '[') {
    _json += ',';
  }
  appendString(key);
  _json += ':';
}

void
Json::append(bool value)
{
  _json += value ? "true" : "false";
}

void
Json::append(char *value)
{
  append(static_cast<const char *>(value));
}

void
Json::append(const char *value)
{
  appendString(value == NULL ? std::string_view() :
                               std::string_view(value));
}

void
Json::appendString(std::string_view value)
{
  _json += '"';
  for (char c : value) {
    unsigned char ch = (unsigned char)c;
    switch (ch) {
    case '"':
      _json += "\\\"";
      break;
    case '\\':
      _json += "\\\\";
      break;
    case '\b':
      _json += "\\b";
      break;
    case '\f':
      _json += "\\f";
      break;
    case '\n':
      _json += "\\n";
      break;
    case '\r':
      _json += "\\r";
      break;
    case '\t':
      _json += "\\t";
      break;
    default:
      if (ch < 0x20) {
        char buf[8];
        snprintf(buf, sizeof(buf), "\\u%04x", ch);
        _json += buf;
      } else {
        _json += c;
      }
      break;
    }
  }
  _json += '"';
}

void
Json::append(std::string_view value)
{
  appendString(value);
}

} // namespace dmtcp
