/****************************************************************************
 * TODO: Replace this header with appropriate header showing MIT OR BSD     *
 *       License                                                            *
 *   Copyright (C) 2006-2008 by Jason Ansel, Kapil Arya, and Gene Cooperman *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu              *
 *                                                                          *
 * This file, dmtcp.h, is placed in the public domain.                *
 * The motivation for this is to allow anybody to freely use this file      *
 * without restriction to statically link this file with any software.      *
 * This allows that software to communicate with the DMTCP libraries.       *
 * -  Jason Ansel, Kapil Arya, and Gene Cooperman                           *
 *      jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu           *
 ****************************************************************************/

#pragma once

#ifndef __DMTCP_KVDB_H__
#define __DMTCP_KVDB_H__

#include <sys/types.h>
#include "dmtcpalloc.h"

namespace dmtcp {
namespace kvdb {
enum class KVDBRequest {
  GET,
  SET,
  INCRBY,
  AND,
  OR,
  XOR,
  MIN,
  MAX
};

enum class KVDBResponse {
  SUCCESS,
  INVALID_REQUEST,
  DB_NOT_FOUND,
  KEY_NOT_FOUND
};

KVDBResponse request64(KVDBRequest request,
                       string const& id,
                       string const& key,
                       int64_t val,
                       int64_t *oldVal = nullptr);

KVDBResponse get64(string const& id, string const& key, int64_t *oldVal);

KVDBResponse set64(string const& id,
                   string const& key,
                   int64_t val,
                   int64_t *oldVal = nullptr);

KVDBResponse request(KVDBRequest request,
                     string const& id,
                     string const& key,
                     string const& val,
                     string *oldVal = nullptr);

KVDBResponse get(string const& id, string const& key, string *oldVal);

KVDBResponse set(string const& id,
                 string const& key,
                 string const& val,
                 string *oldVal = nullptr);

ostream &operator<<(ostream &o, const KVDBRequest &id);
ostream &operator<<(ostream &o, const KVDBResponse &id);
}
}

#endif // #ifdef __DMTCP_KVDB_H__
