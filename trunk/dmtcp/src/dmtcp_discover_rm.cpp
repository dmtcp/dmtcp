#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string>
#include <map>
#include <iostream>
#include <string>
#include <fstream>
#include <vector>
#include <algorithm>
#include <getopt.h>
#include <string.h>

using namespace std;

string warning;

class resources {
public:

  typedef enum {
    input, torque, slurm, sge
  } res_manager_t;
  typedef unsigned long ulong;
  typedef unsigned int uint;
  typedef unsigned short ushort;

  class node_t {
  public:
    string name;
    uint slots;
    uint srv_slots;
    uint id;
    string mode;
    bool is_launch;

    node_t()
    {
      name = "";
      slots = srv_slots = 0;
      id = 0;
      mode = "";
      is_launch = false;
    }
  };

  typedef vector< vector<uint> > mapping_t;

protected:
  res_manager_t _type;
  typedef map<string, node_t> node_map_t;
  node_map_t node_map;
  uint slots_cnt;
  vector<node_t *> sorted_v;

  static bool compare(node_t *l, node_t *r)
  {
    if( l->is_launch )
      return true;
    if( r->is_launch )
      return false;
    if (l->slots > r->slots)
      return true;
    return false;
  }

  void update_sorted()
  {
    sorted_v.clear();
    node_map_t::iterator it = node_map.begin();
    slots_cnt = 0;

    for (; it != node_map.end(); it++) {
      sorted_v.push_back(&(it->second));
      slots_cnt += it->second.slots;
    }

    sort(sorted_v.begin(), sorted_v.end(), compare);
    
    // output_sorted("sorted");
  }

public:

  resources(res_manager_t type)
  {
    _type = type;
    node_map.clear();
    sorted_v.clear();
    slots_cnt = 0;
  }

  ~resources()
  {
    node_map.clear();
  }

  res_manager_t type()
  {
    return _type;
  }

  const char *type_str()
  {
    switch (_type) {
    case torque:
      return "TORQUE";
    case sge:
      return "SGE";
    case slurm:
      return "SLURM";
    default:
      return "NONE";
    }
  }

  uint get_node_count()
  {
    return node_map.size();
  }

  bool get_node(ulong node_num, node_t &node)
  {
    node_map_t::iterator it = node_map.begin();
    ulong cnt;
    for (cnt = 0; it != node_map.end() && cnt < node_num; it++, cnt++);

    if (cnt < node_num)
      return false;

    node = it->second;
    return true;
  }

  node_map_t *get_node_map_copy()
  {
    return new node_map_t(node_map);
  }

  void output(string env_name)
  {
    node_map_t::iterator it = node_map.begin();
    printf("%s=\"", env_name.c_str());
    for (; it != node_map.end(); it++) {
      if( it->second.is_launch )
        printf("*");
      printf("%s:%u ", it->second.name.c_str(), it->second.slots);
    }
    printf("\"\n");
  }

  void output_sorted(string env_name)
  {
    vector<node_t*>::iterator it = sorted_v.begin();
    printf("%s=\"", env_name.c_str());
    for (; it != sorted_v.end(); it++) {
      if( (*it)->is_launch )
        printf("*");
      printf("%s:%u ", (*it)->name.c_str(), (*it)->slots);
    }
    printf("\"\n");
  }

  node_t operator [] (size_t index)
  {
    if (index < sorted_v.size())
      return *sorted_v[index];
    else {
      node_t ret;
      return ret;
    }
  }

  size_t ssize()
  {
    return sorted_v.size();
  }

  bool map_to(resources &newres, mapping_t &map)
  {
    newres.update_sorted();
    update_sorted();

    if (slots_cnt > newres.slots_cnt)
      return false;

    size_t size = newres.node_map.size();
    map.resize(size);
    for (int i = 0; i < size; i++) {
      map[i].clear();
    }
    uint map_used[size];
    for (int i = 0; i < size; i++)
      map_used[i] = 0;

    // map old launch node to new launch node
    uint old_launch, new_launch;
    for (int i = 0; i < sorted_v.size(); i++) {
      if (sorted_v[i]->is_launch) {
        old_launch = i;
        break;
      }
    }
    for (int i = 0; i < newres.ssize(); i++) {
      if (newres[i].is_launch) {
        new_launch = i;
        break;
      }
    }

    if (newres[new_launch].slots < sorted_v[old_launch]->slots) {
      warning += "WARINIG: amount of MPI-worker slots on new node is less than amount on old one";
      // put only launch process on launch node ?
    }

    map[new_launch].push_back(old_launch);
    map_used[new_launch] += sorted_v[old_launch]->slots;

    // map other nodes
    for (int i = 0; i < sorted_v.size(); i++) {
      // skip launch node
      if (i == old_launch)
        continue;

      // continue with any other node
      uint search_res = operator [](i).slots;
      bool found = false;
      for (int j = 0; j < size && !found; j++) {
        uint free = newres[j].slots - map_used[j];
        if (free >= search_res) {
          map_used[j] += search_res;
          map[j].push_back(i);
          found = true;
        }
      }
      if (!found)
        return false;
    }
    return true;
  }

  virtual int discover() = 0;
};

#define MAX_LINE_LEN 1024

class resources_tm : public resources {
public:

  resources_tm() : resources(torque)
  {
  };

  int discover()
  {
    char buf[MAX_LINE_LEN];
    streamsize max_len = MAX_LINE_LEN;
    ulong node_id = 0;
    bool is_launch = true;

    /* try to detect the default directory */
    const char *nodefile = getenv("PBS_NODEFILE");
    if (nodefile == NULL)
      return -1;

    ifstream s(nodefile);

    if (!s.is_open())
      return -1;

    s.getline(buf, max_len);
    while (!s.eof()) {
      if (s.fail() && !s.bad()) {
        // fail bit is set: too big string. Drop the rest
        fprintf(stderr, "Error: reading from PE HOSTFILE: too long string\n");
        while (s.fail() && !s.bad())
          s.getline(buf, max_len);
      } else if (s.bad()) {
        return -1;
      } else {
        if (node_map.find(buf) != node_map.end()) {
          node_map[buf].slots++;
        } else {
          node_map[buf].id = node_id;
          node_id++;
          node_map[buf].slots = 1;
          node_map[buf].name = buf;
          // first node in the list considered as node
          // that launches all application
          node_map[buf].is_launch = is_launch;
          is_launch = false;
        }
        s.getline(buf, max_len);
      }
    }
    return 0;
  }

  static bool probe()
  {
    return (getenv("PBS_ENVIRONMENT") != NULL) &&
            (NULL != getenv("PBS_JOBID"));
  }
};

class sge_tm : public resources {
public:

  sge_tm() : resources(sge)
  {
  };

  int discover()
  {
    char buf[MAX_LINE_LEN];
    char *name, *slots, *blank, *t;
    streamsize max_len = MAX_LINE_LEN;
    ulong node_id = 0;
    bool is_launch = true;

    /* try to detect the default directory */
    const char *nodefile = getenv("PE_HOSTFILE");
    if (nodefile == NULL)
      return -1;

    ifstream s(nodefile);

    if (!s.is_open())
      return -1;

    while (s.getline(buf, max_len)) {
      name = strtok_r(buf, " \n", &t);
      slots = strtok_r(NULL, " \n", &t);
      blank = strtok_r(NULL, " \n", &t);
      blank = strtok_r(NULL, " \n", &t);

      if (node_map.find(name) != node_map.end()) {
        node_map[name].slots += (uint)strtoul(slots, (char **)NULL, 10);
      } else {
        node_map[name].id = node_id;
        node_id++;
        node_map[name].slots = (uint)strtoul(slots, (char **)NULL, 10);
        node_map[name].name = name;
        // first node in the list considered as node
        // that launches all application
        node_map[buf].is_launch = is_launch;
        is_launch = false;
      }
    }
    return 0;
  }

  static bool probe()
  {
    return (getenv("ENVIRONMENT") != NULL) &&
            (NULL != getenv("JOB_ID"));
  }
};

class resources_input : public resources {
private:
  bool _valid;
  map<string, string> node_ckpt_map;

  void trim(string &str, string delim)
  {
    size_t first = 0;

    first = str.find_first_of(delim, first);
    while (first != string::npos) {
      size_t last = str.find_first_not_of(delim, first);
      if (last != string::npos) {
        str.erase(first, last - first);
        first = str.find_first_of(delim, first);
      } else {
        str.erase(first, str.length() - first);
        first = string::npos;
      }
    }
  }

  bool get_checkpoint_filename(string &str, string &ckptname)
  {
    size_t pos = str.find_last_of("/");
    if (pos != string::npos) {
      ckptname.clear();
      ckptname.insert(0, str, pos + 1, str.length() - pos + 1);
      return true;
    }
    return false;
  }

  bool is_serv_slot(string &str)
  {
    string serv_names[] = {"orted", "orterun", "mpiexec", "mpirun"};
    uint size = sizeof (serv_names) / sizeof (serv_names[0]);
    uint i;
    for (i = 0; i < size; i++) {
      if (str.find("ckpt_" + serv_names[i]) != string::npos)
        return true;
    }
    return false;
  }

  bool is_launch_process(string &str)
  {
    string serv_names[] = {"orterun", "mpiexec", "mpirun"};
    uint size = sizeof (serv_names) / sizeof (serv_names[0]);
    uint i;
    for (i = 0; i < size; i++) {
      if (str.find("ckpt_" + serv_names[i]) != string::npos)
        return true;
    }
    return false;
  }

  bool count_slots(string &str, uint &slots, uint &srv_slots, bool &is_launch)
  {
    string delim = " ";
    slots = srv_slots = 0;
    size_t start_pos = 0, match_pos;
    str += ' ';
    if ((start_pos = str.find_first_not_of(delim, start_pos)) == string::npos)
      return false;
    while (start_pos != string::npos && (match_pos = str.find_first_of(delim, start_pos)) != string::npos) {
      size_t sublen = match_pos - start_pos;
      if (sublen > 0) {
        string sub(str.substr(start_pos, sublen));
        string ckptname;
        if (get_checkpoint_filename(sub, ckptname)) {
          if (is_serv_slot(ckptname)) {
            is_launch = is_launch_process(ckptname);
            srv_slots++;
          } else
            slots++;
        }
      }
      start_pos = match_pos;
      start_pos = str.find_first_not_of(delim, start_pos);
    }
  }

  bool add_host(string &str, uint &node_id)
  {
    string delim = ":";
    size_t start_pos = 0;
    size_t match_pos;
    string hostname = "";
    string mode = "";
    bool is_launch;

    // get host name
    if ((match_pos = str.find(delim)) == string::npos)
      return false;
    if (match_pos - start_pos > 0) {
      size_t sublen = match_pos - start_pos;
      hostname = str.substr(start_pos, sublen);
      trim(hostname, " \n\t"); // delete spaces, newlines and tabs
    } else {
      return false;
    }
    start_pos = match_pos + delim.length();

    // skip mode
    if ((match_pos = str.find(delim, start_pos)) == string::npos)
      return false;
    if (!(match_pos - start_pos > 0))
      return false;
    else {
      size_t sublen = match_pos - start_pos;
      mode = str.substr(start_pos, sublen);
    }
    start_pos = match_pos + delim.length();

    // process checkpoints
    size_t sublen = str.length() - start_pos;
    string ckpts(str.substr(start_pos, sublen));
    trim(ckpts, "\n");
    uint slots, srv_slots;
    count_slots(ckpts, slots, srv_slots, is_launch);

    if (node_map.find(hostname) != node_map.end()) {
      node_map[hostname].slots += slots;
      node_map[hostname].srv_slots += srv_slots;
      node_map[hostname].is_launch = node_map[hostname].is_launch || is_launch;
      node_ckpt_map[hostname] += ' ' + ckpts;
    } else {
      node_map[hostname].id = node_id;
      node_id++;
      node_map[hostname].slots = slots;
      node_map[hostname].srv_slots = srv_slots;
      node_map[hostname].name = hostname;
      node_map[hostname].mode = mode;
      node_map[hostname].is_launch = is_launch;
      node_ckpt_map[hostname] = ckpts;
    }

    return true;
  }

public:

  resources_input(string str) : resources(input)
  {
    string delim = "::";
    uint hostid = 0;

    _valid = false;
    size_t start_pos = 0, match_pos;

    if ((match_pos = str.find(delim)) == string::npos)
      return;
    start_pos = match_pos + delim.length();
    while ((match_pos = str.find(delim, start_pos)) != string::npos) {
      size_t sublen = match_pos - start_pos;
      if (sublen > 0) {
        string sub(str.substr(start_pos, sublen));
        if (add_host(sub, hostid))
          _valid = true;
      }
      start_pos = match_pos + delim.length();
    }

    if (start_pos != str.length()) {
      size_t sublen = str.length() - start_pos;
      if (sublen > 0) {
        string sub(str.substr(start_pos, sublen));
        if (add_host(sub, hostid))
          _valid = true;
      }
      start_pos = match_pos + delim.length();
    }
  }

  int discover()
  {
    return 0;
  }

  bool valid()
  {
    return _valid;
  }

  int writeout(string env_var, resources &r)
  {
    mapping_t map;

    if (!map_to(r, map))
      return false;

    cout << env_var + "=\'" << endl;
    for (int i = 0; i < r.ssize(); i++) {
      if (map[i].size()) {
        cout << ":: " + r[i].name + " :" + sorted_v[0]->mode + ": ";
        for (int j = 0; j < map[i].size(); j++) {
          int k = map[i][j];
          string name = sorted_v[k]->name;
          cout << node_ckpt_map[name];
        }
        cout << endl;
      }
    }

    cout << "\'" << endl;
  }
};

resources *discover_rm()
{
  if (resources_tm::probe() == true) {
    return new resources_tm();
  } else if (sge_tm::probe() == true) {
    return new sge_tm();
  }

  return NULL;
}

void print_help(char *pname)
{
  string name = pname;
  cout << "Usage: " + name << endl;
  cout << "--help or no arguments - print this page" << endl;
  cout << "-t, --test-rm          - check for rm and write out allocated nodes" << endl;
  cout << "no options mean read worker_ckpts content from input and do" << endl;
  cout << "mapping of exiting RM resources to old ones" << endl;
}

int main(int argc, char **argv)
{
  resources *rm = discover_rm();

  enum mode_t {
    help, rmtest, full
  } mode = help;

  while (1) {
    char c;
    int option_index;
    static struct option long_options[] = {
      // modes
      { "help", 0, 0, 'h'},
      { "test-rm", 1, 0, 't'},
      { 0, 0, 0, 0}
    };

    c = getopt_long(argc, argv, "ht", long_options, &option_index);
    if (c == -1)
      break;
    switch (c) {
    case 'h':
      break;
    case 't':
      mode = rmtest;
      break;
    }
  }

  if(optind < argc && mode == help) {
    mode = full;
  }


  switch (mode) {
  case help:
    print_help(argv[0]);
    break;
  case rmtest:
    if (rm == NULL || rm->discover()) {
      printf("RES_MANAGER=NONE\n");
    } else {
      printf("RES_MANAGER=%s\n", rm->type_str());
      rm->output("manager_rsources");
      fflush(stdout);
    }
    break;
  case full:
    if (rm == NULL || rm->discover()) {
      printf("RES_MANAGER=NONE\n");
    } else {
      printf("RES_MANAGER=%s\n", rm->type_str());
      rm->output("manager_resources");
      fflush(stdout);
    }
    resources_input inp(argv[1]);
    inp.output("input_config");
    resources::mapping_t map;
    inp.writeout("new_worker_ckpts", *rm);
    fflush(stdout);
    break;
  }
  return 0;
}
