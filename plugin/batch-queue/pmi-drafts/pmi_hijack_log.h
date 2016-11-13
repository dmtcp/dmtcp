#ifndef PMI_HIJACK_H
#define PMI_HIJACK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fstream>
#include <iostream>
#include <vector>

class pmi_hijack_log
{
  public:
    typedef enum { KVS_NONE = 0, KVS_CREATE = 1, KVS_DESTROY, KVS_PUT,
                   KVS_COMMIT } etype_t;

    typedef struct {
      etype_t type;
      char *kvsname, *key, *value;
    } record_t;

    std::string etype2str(etype_t t)
    {
      switch (t) {
      case KVS_NONE:
        return "KVS_NONE";

      case KVS_CREATE:
        return "KVS_CREATE";

      case KVS_DESTROY:
        return "KVS_DESTROY";

      case KVS_PUT:
        return "KVS_PUT";

      case KVS_COMMIT:
        return "KVS_COMMIT";
      }
      return "BAD e_type!";
    }

  protected:
    std::vector<record_t>log;
    typedef std::vector<record_t>::iterator iter_t;

  public:
    pmi_hijack_log()
    {
      // log.clear();
    }

    ~pmi_hijack_log()
    {
      iter_t it = log.begin();

      for (; it != log.end(); it++) {
        if ((*it).kvsname) {
          free((*it).kvsname);
        }
        if ((*it).key) {
          free((*it).key);
        }
        if ((*it).value) {
          free((*it).value);
        }
      }
    }

    int kvs_create(char *kvsname)
    {
      char *ptr = strdup(kvsname);

      if (!ptr) {
        fprintf(stderr, "kvs_create: Cannot strdup\n");
        exit(0);
      }
      printf("KVS_LOG: %s: %s\n", __FUNCTION__, kvsname);
      record_t rec = { KVS_CREATE, ptr, NULL, NULL };
      log.push_back(rec);
    }

    int kvs_destroy(char *kvsname)
    {
      char *ptr = strdup(kvsname);

      if (!ptr) {
        fprintf(stderr, "kvs_create: Cannot strdup\n");
        exit(0);
      }
      printf("KVS_LOG: %s: %s\n", __FUNCTION__, kvsname);
      record_t rec = { KVS_DESTROY, ptr, NULL, NULL };
      log.push_back(rec);
    }

    int kvs_put(const char *kvsname, const char *key, const char *val)
    {
      char *lname = strdup(kvsname);
      char *lkey = strdup(key);
      char *lval = strdup(val);

      if (!lname || !lkey || !lval) {
        fprintf(stderr, "kvs_put: Cannot strdup\n");
        exit(0);
      }
      printf("KVS_LOG: %s: %s: %s = %s\n", __FUNCTION__, kvsname, key, val);
      record_t rec = { KVS_PUT, lname, lkey, lval };
      log.push_back(rec);
      printf("KVS_LOG: %s: %s: %s = %s. Log size = %d\n",
             __FUNCTION__,
             kvsname,
             key,
             val,
             log.size());
    }

    int kvs_commit(char *kvsname)
    {
      char *ptr = strdup(kvsname);

      if (!ptr) {
        fprintf(stderr, "kvs_create: Cannot strdup\n");
        exit(0);
      }
      printf("KVS LOG: %s: %s. Log size = %d\n",
             __FUNCTION__,
             kvsname,
             log.size());
      record_t rec = { KVS_COMMIT, ptr, NULL, NULL };
      log.push_back(rec);
    }

    int serialize(std::fstream &out, int kvsname_max, int key_max, int val_max)
    {
      char kvsname[kvsname_max], key[key_max], value[val_max];

      out << kvsname_max << " " << key_max << " " << val_max << std::endl;
      printf("KVS_LOG: %s: number of records: %d \n", __FUNCTION__, log.size());
      iter_t it = log.begin();
      for (; it != log.end(); it++) {
        switch ((*it).type) {
        case KVS_CREATE: {
          out << KVS_CREATE << " ";
          out << (*it).kvsname << std::endl;
          printf("KVS_LOG: %s: save KVS_Create\n", __FUNCTION__);
          break;
        }
        case KVS_DESTROY: {
          out << KVS_DESTROY << " ";
          out << (*it).kvsname << std::endl;
          printf("KVS_LOG: %s: save KVS_Destroy\n", __FUNCTION__);
          break;
        }
        case KVS_COMMIT: {
          out << KVS_COMMIT << " ";
          out << (*it).kvsname << std::endl;
          printf("KVS_LOG: %s: save KVS_Commit\n", __FUNCTION__);
          break;
        }
        case KVS_PUT: {
          out << KVS_PUT << " ";
          out << (*it).kvsname << " ";
          out << (*it).key << " ";
          out << (*it).value << std::endl;
          printf("KVS_LOG: %s: save KVS_Put\n", __FUNCTION__);
          break;
        }
        }
      }
    }

    int deserialize(std::fstream &in,
                    int &kvsname_max,
                    int &key_max,
                    int &val_max)
    {
      in >> kvsname_max >> key_max >> val_max;
      std::cout << "MAX Lengths: " << kvsname_max << ", " << key_max << ", " <<
        val_max << std::endl;
      char kvsname[kvsname_max], key[key_max], value[val_max];
      while (!in.eof()) {
        record_t rec = { KVS_NONE };
        int tmp;
        in >> tmp;
        rec.type = (etype_t)tmp;
        switch (rec.type) {
        case KVS_CREATE:
        case KVS_DESTROY:
        case KVS_COMMIT: {
          in >> kvsname;
          rec.kvsname = strdup(kvsname);
          rec.key = strdup("");
          rec.value = strdup("");
          log.push_back(rec);
          break;
        }
        case KVS_PUT: {
          in >> kvsname >> key >> value;
          rec.kvsname = strdup(kvsname);
          rec.key = strdup(key);
          rec.value = strdup(value);
          log.push_back(rec);
        }
        }
        std::cout << etype2str(rec.type) << "[" << rec.kvsname << "]: ";
        std::cout << rec.key << ", " << rec.value << std::endl;
      }
    }

    int iterate_init()
    {
      return 0;
    }

    int get_and_shift(int &hndl, record_t &rec)
    {
      if (hndl >= log.size() || hndl < 0) {
        return -1;
      }
      rec = log[hndl];
      hndl++;
      return 0;
    }
};
#endif // ifndef PMI_HIJACK_H
