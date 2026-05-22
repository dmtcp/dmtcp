#!/usr/bin/env python3
"""Executable gate for moving the PID internal plugin into libdmtcp.so.

Modes:
  static     source/config-only checks for final PID built-in state
  artifacts  built-output stale libdmtcp_pid.so checks
  helpers    _real_* helper ownership/collision checks
  fork       fork/vfork single-owner checks
  wrappers   wait/fcntl/syscall single-owner checks
  overlap    timer/SysV/POSIX-mq PID overlap checks
  runtime    run the launch/exec preload probe in normal and disable-all modes
  self-test  inline negative fixtures for the gate itself
  full       all checks, including runtime when built artifacts exist
"""
from __future__ import print_function

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
HIDDEN = ('.git', '.gsd', '.planning', '.audits')

PID_SOURCES = (
  'src/plugin/pid/glibc_pthread.cpp',
  'src/plugin/pid/glibc_pthread.h',
  'src/plugin/pid/pid.cpp',
  'src/plugin/pid/pid_filewrappers.cpp',
  'src/plugin/pid/pid.h',
  'src/plugin/pid/pid_miscwrappers.cpp',
  'src/plugin/pid/pid_syscallsreal.c',
  'src/plugin/pid/pidwrappers.cpp',
  'src/plugin/pid/pidwrappers.h',
  'src/plugin/pid/sched_wrappers.cpp',
  'src/plugin/pid/virtualpidtable.cpp',
  'src/plugin/pid/virtualpidtable.h',
)
PID_MAKE_SOURCES = tuple(p.replace('src/', '', 1) for p in PID_SOURCES)
PID_CPP_SOURCES = tuple(p for p in PID_MAKE_SOURCES if p.endswith('.cpp'))
PID_C_SOURCES = tuple(p for p in PID_MAKE_SOURCES if p.endswith('.c'))
PID_OBJECTS = tuple(p.replace('.cpp', '.$(OBJEXT)').replace('.c', '.$(OBJEXT)')
                    for p in PID_CPP_SOURCES + PID_C_SOURCES)
PID_DEPFILES = tuple(
  p.rsplit('/', 1)[0] + '/$(DEPDIR)/' +
  p.rsplit('/', 1)[1].replace('.cpp', '.Po').replace('.c', '.Po')
  for p in PID_CPP_SOURCES + PID_C_SOURCES
)

STATIC_INPUTS = (
  'include/dmtcp.h',
  'src/Makefile.am', 'src/Makefile.in', 'src/Makefile',
  'src/plugin/Makefile.am', 'src/plugin/Makefile.in', 'src/plugin/Makefile',
  'src/dmtcp_launch.cpp', 'src/execwrappers.cpp', 'src/miscwrappers.cpp',
  'src/threadlist.cpp', 'src/wrappers.cpp', 'src/syscallsreal.c',
  'src/syscallwrappers.h', 'src/pluginmanager.cpp',
  'src/plugin/timer/timerwrappers.cpp', 'src/plugin/timer/timerwrappers.h',
  'src/plugin/svipc/sysvipc.cpp',
  'src/plugin/svipc/sysvipcwrappers.cpp', 'src/plugin/svipc/sysvipcwrappers.h',
  'src/plugin/ipc/file/posixipcwrappers.cpp', 'src/plugin/ipc/file/filewrappers.h',
) + PID_SOURCES

TEST_INPUTS = ('test/Makefile.in', 'test/Makefile', 'test/pid-built-in-preload.c')
FORBIDDEN_DSO = ('libdmtcp_pid.so', 'libdmtcp_pid')
PROBE = 'test/pid-built-in-preload'
PROBE_SOURCE = 'test/pid-built-in-preload.c'
PID_ACCESSOR = 'dmtcp_Pid_PluginDescr'

NON_POD_FILE_SCOPE_TYPES = (
  'string', 'std::string', 'dmtcp::string',
  'vector', 'std::vector',
  'map', 'std::map',
  'multimap', 'std::multimap',
  'unordered_map', 'std::unordered_map',
  'unordered_multimap', 'std::unordered_multimap',
  'set', 'std::set',
  'multiset', 'std::multiset',
  'unordered_set', 'std::unordered_set',
  'unordered_multiset', 'std::unordered_multiset',
  'list', 'std::list',
  'deque', 'std::deque',
  'queue', 'std::queue',
  'priority_queue', 'std::priority_queue',
  'stack', 'std::stack',
)
NON_POD_BASE_TYPES = tuple(sorted({t.split('::')[-1] for t in NON_POD_FILE_SCOPE_TYPES}))

HELPER_FILES = ('src/syscallsreal.c', 'src/plugin/pid/pid_syscallsreal.c')
HELPER_COLLISION_NAMES = (
  'fork', 'vfork', 'wait', 'waitpid', 'waitid', 'wait3', 'wait4', 'fcntl',
  'syscall', 'shmget', 'shmat', 'shmdt', 'shmctl', 'semctl', 'msgctl',
  'mq_notify', 'clock_getcpuclockid', 'timer_create', 'pthread_cancel',
  'pthread_exit',
)

FORK_OWNER_FILES = ('src/execwrappers.cpp', 'src/plugin/pid/pid_miscwrappers.cpp')
FORK_SYMBOLS = ('fork', 'vfork')
WRAPPER_OWNER_FILES = (
  'src/miscwrappers.cpp', 'src/wrappers.cpp', 'src/plugin/pid/pidwrappers.cpp',
  'src/plugin/pid/pid_miscwrappers.cpp',
)
WRAPPER_SYMBOLS = ('wait', 'waitpid', 'waitid', 'wait3', 'wait4', 'fcntl', 'syscall')
OVERLAP_OWNER_FILES = (
  'src/plugin/timer/timerwrappers.cpp',
  'src/plugin/svipc/sysvipcwrappers.cpp',
  'src/plugin/ipc/file/posixipcwrappers.cpp',
  'src/plugin/pid/pid_miscwrappers.cpp',
)
OVERLAP_SYMBOLS = (
  'timer_create', 'clock_getcpuclockid', 'pthread_getcpuclockid',
  'shmctl', 'semctl', 'msgctl', 'mq_notify',
)

class Result(object):
  def __init__(self, emit=True):
    self.passes = 0
    self.failures = []
    self.emit = emit
  def pass_(self, contract, msg):
    self.passes += 1
    if self.emit:
      print('PASS {0}: {1}'.format(contract, msg))
  def fail(self, contract, msg, details=None):
    self.failures.append((contract, msg, details or []))
    if self.emit:
      print('FAIL {0}: {1}'.format(contract, msg))
      for detail in details or []:
        print('  - {0}'.format(detail))
  def finish(self):
    if self.failures:
      print('FAIL summary: {0} contract(s) failed; {1} contract(s) passed.'.format(
        len(self.failures), self.passes))
      return 1
    print('PASS summary: {0} contract(s) passed.'.format(self.passes))
    return 0

class Reader(object):
  def __init__(self, fixtures=None):
    self.fixtures = fixtures
  def read_text(self, rel, result, contract):
    if self.fixtures is not None and rel in self.fixtures:
      return self.fixtures[rel]
    try:
      return (REPO_ROOT / rel).read_text(encoding='utf-8')
    except Exception as exc:
      result.fail(contract, 'required file is not readable', ['{0}: {1}'.format(rel, exc)])
      return ''
  def read_static(self, rel, result, contract):
    if rel not in STATIC_INPUTS:
      result.fail('static-input-scope', 'static mode attempted to inspect an unapproved path', [rel])
      return ''
    return self.read_text(rel, result, contract)
  def read_test(self, rel, result, contract):
    if rel not in TEST_INPUTS:
      result.fail('test-input-scope', 'test mode attempted to inspect an unapproved path', [rel])
      return ''
    return self.read_text(rel, result, contract)

def strip_comments(text):
  def repl(match):
    return ''.join('\n' if ch == '\n' else ' ' for ch in match.group(0))
  text = re.sub(r'/\*.*?\*/', repl, text, flags=re.S)
  return re.sub(r'//.*', '', text)

def make_var(text, name):
  lines = text.splitlines()
  for i, line in enumerate(lines):
    if line.startswith(name + ' ='):
      block = [line]
      while block[-1].rstrip().endswith('\\') and i + 1 < len(lines):
        i += 1
        block.append(lines[i])
      return '\n'.join(block)
  return ''

def braced_body(text, open_idx):
  depth = 0
  for i in range(open_idx, len(text)):
    if text[i] == '{':
      depth += 1
    elif text[i] == '}':
      depth -= 1
      if depth == 0:
        return text[open_idx + 1:i]
  return None

def has_function_definition(text, symbol):
  clean = strip_comments(text)
  pattern = re.compile(r'(?:^|\n)\s*(?:extern\s+"C"\s*)?(?:[A-Za-z_][\w:<>,~\s\*&]*\s+)?' +
                       re.escape(symbol) + r'\s*\(', re.M)
  for match in pattern.finditer(clean):
    brace = clean.find('{', match.end())
    semi = clean.find(';', match.end())
    if brace != -1 and (semi == -1 or brace < semi):
      if braced_body(clean, brace) is not None:
        return True
  return False

def helper_defs(text):
  names = set()
  for name in re.findall(r'\b_real_([A-Za-z0-9_]+)\s*\(', strip_comments(text)):
    names.add(name)
  return names

def require_items(result, reader, rel, var, items, contract):
  block = make_var(reader.read_static(rel, result, contract), var)
  if not block:
    result.fail(contract, 'Makefile variable is missing', ['{0}: {1}'.format(rel, var)])
    return
  missing = [x for x in items if x not in block]
  if missing:
    result.fail(contract, 'Makefile variable is missing PID built-in entries',
                ['{0}: missing {1}'.format(rel, x) for x in missing])
    return
  result.pass_(contract, '{0}:{1} contains {2} PID entries'.format(rel, var, len(items)))

def require_absent(result, text, rel, needles, contract):
  hits = []
  for needle in needles:
    for lineno, line in enumerate(text.splitlines(), 1):
      if needle in line:
        hits.append('{0}:{1}: {2}'.format(rel, lineno, line.strip()))
  if hits:
    result.fail(contract, 'stale PID DSO token is present', hits)
  else:
    result.pass_(contract, '{0} omits stale PID DSO tokens'.format(rel))

def line_start_depths(text):
  clean = strip_comments(text)
  depth = 0
  for lineno, line in enumerate(clean.splitlines(), 1):
    start_depth = depth
    for ch in line:
      if ch == '{':
        depth += 1
      elif ch == '}':
        depth = max(0, depth - 1)
    yield lineno, line.strip(), start_depth

def is_non_pod_pointer_declaration(line):
  base = r'(?:' + '|'.join(re.escape(t) for t in NON_POD_BASE_TYPES) + r')'
  return re.search(
    r'\bstatic\s+(?:const\s+|volatile\s+)*(?:(?:std|dmtcp)::)?' + base +
    r'(?:\s*<[^;{}()]*>)?\s*\*\s*[A-Za-z_]\w*\s*(?:=\s*(?:NULL|nullptr|0))?\s*;',
    line)

def file_scope_non_pod_hazards(rel, text):
  details = []
  base = r'(?:' + '|'.join(re.escape(t) for t in NON_POD_BASE_TYPES) + r')'
  string_object = re.compile(
    r'\b(?:static\s+)?(?:const\s+|volatile\s+)*(?:(?:std|dmtcp)::)?string\s+([A-Za-z_]\w*)\s*(?:[=;\[])')
  container_object = re.compile(
    r'\b(?:static\s+)?(?:const\s+|volatile\s+)*(?:(?:std|dmtcp)::)?' + base +
    r'\s*<[^;{}()]*>\s+([A-Za-z_]\w*)\s*(?:[=;\[])')
  for lineno, line, depth in line_start_depths(text):
    if depth != 0 or not line or line.startswith('#'):
      continue
    if is_non_pod_pointer_declaration(line):
      continue
    matched = string_object.search(line) or container_object.search(line)
    if matched:
      details.append('{0}:{1}: file-scope non-POD C++ object: {2}'.format(
        rel, lineno, line))
  return details

def validate_pid_static_initialization(result, reader):
  details = []
  for rel in PID_SOURCES:
    if not rel.endswith(('.cpp', '.h')):
      continue
    text = reader.read_static(rel, result, 'pid-static-initialization')
    details.extend(file_scope_non_pod_hazards(rel, text))
  if details:
    result.fail(
      'pid-static-initialization',
      'PID built-in source contains file-scope non-POD C++ static-initialization hazards',
      details)
  else:
    result.pass_(
      'pid-static-initialization',
      'PID source set has no file-scope string/STL container objects; reviewed POD and lazy pointer state remain allowed')

def validate_static_scope(result, reader):
  details = []
  seen = set()
  for rel in STATIC_INPUTS:
    p = Path(rel)
    parts = p.parts
    if rel in seen:
      details.append('duplicate static input: {0}'.format(rel))
    seen.add(rel)
    if p.is_absolute() or '..' in parts:
      details.append('non-repository-relative static input: {0}'.format(rel))
    if not parts or parts[0] not in ('include', 'src'):
      details.append('static input is outside include/src: {0}'.format(rel))
    if any(part in HIDDEN for part in parts):
      details.append('static input uses hidden state: {0}'.format(rel))
    if p.suffix not in ('', '.am', '.c', '.cpp', '.h', '.in'):
      details.append('static input suffix is not source/config-like: {0}'.format(rel))
    if reader.fixtures is None and not (REPO_ROOT / rel).is_file():
      details.append('static input does not exist: {0}'.format(rel))
    if reader.fixtures is not None and rel not in reader.fixtures:
      details.append('static fixture is missing: {0}'.format(rel))
  if details:
    result.fail('static-input-scope', 'static input list violates the source-only contract', details)
  else:
    result.pass_('static-input-scope', 'static checks are limited to explicit include/src source/configuration files')

def validate_probe_target(result, reader):
  for rel in ('test/Makefile.in', 'test/Makefile'):
    text = reader.read_test(rel, result, 'probe-make-target')
    if 'pid-built-in-preload: pid-built-in-preload.c' not in text:
      result.fail('probe-make-target', 'PID preload probe target is missing', [rel])
    elif '$(CC) -o $@ $< $(CFLAGS)' not in text and '${CC} -o $@ $< $(CFLAGS)' not in text:
      result.fail('probe-make-target', 'PID preload probe target does not compile with CFLAGS', [rel])
    else:
      result.pass_('probe-make-target', '{0} builds pid-built-in-preload'.format(rel))
  source = reader.read_test(PROBE_SOURCE, result, 'probe-source-contract')
  for token in (FORBIDDEN_DSO[0], 'DMTCP_HIJACK_LIBS', 'DMTCP_PID_BUILT_IN_PRELOAD_AFTER_EXEC', 'fork()', 'waitpid'):
    if token not in source:
      result.fail('probe-source-contract', 'probe source is missing required diagnostic/runtime token', [token])
      return
  result.pass_('probe-source-contract', 'probe source checks preload state, exec reconstruction, fork/wait, and stale PID DSO absence')

def validate_static(result, reader):
  validate_static_scope(result, reader)
  validate_pid_static_initialization(result, reader)
  for rel in ('src/Makefile.am', 'src/Makefile.in', 'src/Makefile'):
    require_items(result, reader, rel, '__d_libdir__libdmtcp_so_SOURCES', PID_MAKE_SOURCES, 'makefile-libdmtcp-pid-sources')
  for rel in ('src/Makefile.in', 'src/Makefile'):
    require_items(result, reader, rel, 'am___d_libdir__libdmtcp_so_OBJECTS', PID_OBJECTS, 'makefile-libdmtcp-pid-objects')
    require_items(result, reader, rel, 'am__depfiles_remade', PID_DEPFILES, 'makefile-libdmtcp-pid-dependencies')
  for rel in ('src/plugin/Makefile.am', 'src/plugin/Makefile.in', 'src/plugin/Makefile', 'src/dmtcp_launch.cpp'):
    require_absent(result, reader.read_static(rel, result, 'stale-pid-dso-source'), rel, FORBIDDEN_DSO, 'stale-pid-dso-source')
  pid_cpp = reader.read_static('src/plugin/pid/pid.cpp', result, 'pid-descriptor-accessor')
  clean_pid_cpp = strip_comments(pid_cpp)
  if PID_ACCESSOR not in clean_pid_cpp:
    result.fail('pid-descriptor-accessor', 'PID descriptor accessor is missing', ['src/plugin/pid/pid.cpp: expected {0}'.format(PID_ACCESSOR)])
  elif 'DMTCP_DECL_PLUGIN' in clean_pid_cpp:
    result.fail('pid-descriptor-accessor', 'PID still declares an external plugin initializer', ['src/plugin/pid/pid.cpp contains DMTCP_DECL_PLUGIN'])
  else:
    result.pass_('pid-descriptor-accessor', 'PID descriptor is exposed through an internal accessor only')
  public_header = reader.read_static('include/dmtcp.h', result, 'pid-accessor-private')
  if PID_ACCESSOR in public_header:
    result.fail('pid-accessor-private', 'PID built-in accessor leaked into public plugin ABI header', ['include/dmtcp.h'])
  else:
    result.pass_('pid-accessor-private', 'PID built-in accessor is not public ABI')
  pm = reader.read_static('src/pluginmanager.cpp', result, 'pluginmanager-pid-last')
  clean_pm = strip_comments(pm)
  if PID_ACCESSOR not in clean_pm:
    result.fail('pluginmanager-pid-last', 'PluginManager does not reference PID descriptor accessor', ['src/pluginmanager.cpp'])
  else:
    pid_idx = clean_pm.rfind(PID_ACCESSOR)
    tail = clean_pm[pid_idx:]
    later_register = re.search(r'dmtcp_register_plugin\s*\(', tail[tail.find(PID_ACCESSOR) + len(PID_ACCESSOR):])
    if later_register:
      result.fail('pluginmanager-pid-last', 'another descriptor registration appears after PID', ['PID must be final built-in registration'])
    else:
      result.pass_('pluginmanager-pid-last', 'PID registration appears last in PluginManager source order')

def validate_helpers(result, reader):
  defs_by_file = {}
  for rel in HELPER_FILES:
    defs_by_file[rel] = helper_defs(reader.read_static(rel, result, 'real-helper-ownership'))
  details = []
  for name in HELPER_COLLISION_NAMES:
    owners = [rel for rel, defs in defs_by_file.items() if name in defs]
    if len(owners) > 1:
      details.append('_real_{0}: {1}'.format(name, ', '.join(owners)))
  if details:
    result.fail('real-helper-ownership', 'PID and central real-helper definitions still collide', details)
  else:
    result.pass_('real-helper-ownership', 'PID real-helper definitions do not collide with central helpers for audited names')

def validate_single_owner(result, reader, contract, symbols, files):
  details = []
  owner_counts = []
  for symbol in symbols:
    owners = []
    for rel in files:
      text = reader.read_static(rel, result, contract)
      if has_function_definition(text, symbol):
        owners.append(rel)
    owner_counts.append((symbol, owners))
    if len(owners) != 1:
      details.append('{0}: expected exactly one owner, found {1}: {2}'.format(symbol, len(owners), ', '.join(owners) or '<none>'))
  if details:
    result.fail(contract, 'wrapper ownership is not single-owner', details)
  else:
    result.pass_(contract, 'single owner for {0}'.format(', '.join(sym for sym, _owners in owner_counts)))

def validate_fork_core_identity(result, reader):
  threadlist = strip_comments(reader.read_static('src/threadlist.cpp', result,
                                                'fork-core-real-pid'))
  pid_cpp = strip_comments(reader.read_static('src/plugin/pid/pid.cpp', result,
                                             'fork-core-real-pid'))
  details = []
  real_pid_assignments = re.findall(
    r'motherpid\s*=\s*(?:\(\s*pid_t\s*\))?\s*_real_syscall\s*\(\s*SYS_getpid\s*\)',
    threadlist)
  virtual_pid_assignments = re.findall(
    r'motherpid\s*=\s*getpid\s*\(', threadlist)
  if len(real_pid_assignments) < 2:
    details.append('src/threadlist.cpp: expected real SYS_getpid motherpid assignment at startup and postRestart')
  if virtual_pid_assignments:
    details.append('src/threadlist.cpp: motherpid must not be assigned through virtualized getpid()')
  require_tokens(details, threadlist, 'src/threadlist.cpp', 'core fork/vfork thread signalling', (
    'THREAD_TGKILL(motherpid', 'TLSInfo_SaveTLSState(curThread)',
    'TLSInfo_RestoreTLSTidPid(motherofall)', '_real_syscall(SYS_kill, motherpid',
    'thread->tid = (pid_t)_real_syscall(SYS_gettid);'))
  require_tokens(details, pid_cpp, 'src/plugin/pid/pid.cpp', 'restart virtual tid refresh', (
    'pid_t virtualTid = VirtualPidTable::gettid();',
    'dmtcp_pthread_set_tid(pthread_self(), virtualTid)'))

  if details:
    result.fail('fork-core-real-pid', 'core thread lifecycle paths may be using PID-virtualized process identity', details)
  else:
    result.pass_('fork-core-real-pid', 'core thread signalling uses real kernel PID while PID virtualization remains user-facing')

def require_tokens(details, text, rel, purpose, tokens):
  missing = [token for token in tokens if token not in text]
  if missing:
    details.append('{0}: missing {1} for {2}'.format(rel, ', '.join(missing), purpose))

def validate_wrapper_composition(result, reader):
  misc = strip_comments(reader.read_static('src/miscwrappers.cpp', result, 'wrapper-composition'))
  wrappers = strip_comments(reader.read_static('src/wrappers.cpp', result, 'wrapper-composition'))
  details = []

  require_tokens(details, misc, 'src/miscwrappers.cpp', 'PID-aware wait ownership', (
    'dmtcp_virtual_to_real_pid', 'dmtcp_real_to_virtual_pid',
    'dmtcp_pid_erase_virtual_pid', 'WNOHANG', '_real_wait4', '_real_waitid'))
  require_tokens(details, wrappers, 'src/wrappers.cpp', 'PID-aware fcntl plus core fd tracking', (
    'F_SETOWN', 'F_GETOWN', 'dmtcp_virtual_to_real_pid',
    'dmtcp_real_to_virtual_pid', 'processDupFd', 'F_DUPFD', 'F_DUPFD_CLOEXEC'))
  require_tokens(details, misc, 'src/miscwrappers.cpp', 'PID-aware syscall routing', (
    'SYS_gettid', 'SYS_tkill', 'SYS_tgkill', 'SYS_getpid', 'SYS_getppid',
    'SYS_getpgid', 'SYS_setpgid', 'SYS_getsid', 'SYS_kill', 'SYS_waitid',
    'SYS_wait4'))

  if details:
    result.fail('wrapper-composition', 'chosen wrapper owners are missing PID translation or core tracking', details)
  else:
    result.pass_('wrapper-composition', 'core wait/fcntl/syscall owners retain PID translation and fd tracking diagnostics')

def validate_overlap_composition(result, reader):
  sysvipc = strip_comments(reader.read_static('src/plugin/svipc/sysvipc.cpp', result, 'overlap-composition'))
  details = []

  require_tokens(details, sysvipc, 'src/plugin/svipc/sysvipc.cpp', 'SysV IPC real PID leader election under built-in PID', (
    'sysvipcRealPid()', 'dmtcp_virtual_to_real_pid',
    'info.shm_lpid == sysvipcRealPid()',
    'sysvipcRealPid() == _real_semctl(_realId, 0, GETPID)',
    'buf.msg_lspid == sysvipcRealPid()'))
  stale_patterns = (
    r'info\.shm_lpid\s*==\s*getpid\s*\(',
    r'getpid\s*\(\s*\)\s*==\s*_real_semctl\s*\([^;]*GETPID',
    r'buf\.msg_lspid\s*==\s*getpid\s*\(',
  )
  for pattern in stale_patterns:
    if re.search(pattern, sysvipc, re.S):
      details.append('src/plugin/svipc/sysvipc.cpp: kernel SysV PID leader election must not compare against virtualized getpid()')

  if details:
    result.fail('overlap-composition', 'SysV IPC overlap paths may be using PID-virtualized leader identity', details)
  else:
    result.pass_('overlap-composition', 'SysV IPC leader election compares kernel PID fields against real PID translation')

def validate_artifacts(result):
  lib_dir = REPO_ROOT / 'lib'
  found = []
  if lib_dir.exists():
    for path in lib_dir.rglob('*'):
      if 'libdmtcp_pid' in path.name:
        found.append(str(path.relative_to(REPO_ROOT)))
  if found:
    result.fail('stale-pid-dso-artifact', 'built PID DSO artifact is still present', found)
  else:
    result.pass_('stale-pid-dso-artifact', 'no libdmtcp_pid artifact found under lib/')
  launcher = REPO_ROOT / 'bin' / 'dmtcp_launch'
  if not launcher.exists():
    result.fail('launcher-string-artifact', 'built dmtcp_launch binary is missing', [str(launcher)])
    return
  try:
    proc = subprocess.run(['strings', str(launcher)], cwd=str(REPO_ROOT), text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
  except OSError as exc:
    result.fail('launcher-string-artifact', 'could not run strings on dmtcp_launch', [str(exc)])
    return
  if 'libdmtcp_pid' in proc.stdout:
    result.fail('launcher-string-artifact', 'dmtcp_launch still embeds libdmtcp_pid', ['strings bin/dmtcp_launch contains libdmtcp_pid'])
  else:
    result.pass_('launcher-string-artifact', 'dmtcp_launch strings omit libdmtcp_pid')

def run_probe(result, args, contract):
  proc = subprocess.run(args, cwd=str(REPO_ROOT), text=True,
                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
  if proc.returncode != 0:
    details = ['command: {0}'.format(' '.join(args)), 'exit: {0}'.format(proc.returncode)]
    if proc.stdout.strip():
      details.append('stdout: {0}'.format(proc.stdout.strip()[-1000:]))
    if proc.stderr.strip():
      details.append('stderr: {0}'.format(proc.stderr.strip()[-1000:]))
    result.fail(contract, 'PID preload runtime probe failed', details)
  else:
    msg = proc.stdout.strip().splitlines()[-1] if proc.stdout.strip() else 'exit 0'
    result.pass_(contract, msg)

def validate_runtime(result):
  probe = REPO_ROOT / PROBE
  launcher = REPO_ROOT / 'bin' / 'dmtcp_launch'
  if not probe.exists():
    result.fail('runtime-probe-built', 'PID preload probe binary is missing', [PROBE])
    return
  if not os.access(str(probe), os.X_OK):
    result.fail('runtime-probe-built', 'PID preload probe is not executable', [PROBE])
    return
  if not launcher.exists():
    result.fail('runtime-probe-launcher', 'dmtcp_launch binary is missing', ['bin/dmtcp_launch'])
    return
  result.pass_('runtime-probe-built', 'PID preload probe and dmtcp_launch exist')
  run_probe(result, ['bin/dmtcp_launch', '--quiet', './' + PROBE], 'runtime-probe-normal')
  run_probe(result, ['bin/dmtcp_launch', '--quiet', '--disable-all-plugins', './' + PROBE], 'runtime-probe-disable-all')

def validate_test_wiring(result, reader):
  validate_probe_target(result, reader)

def self_test():
  fixtures = {
    'include/dmtcp.h': '/* public ABI */\n',
    'src/Makefile.am': '__d_libdir__libdmtcp_so_SOURCES = ' + ' '.join(PID_MAKE_SOURCES) + '\n',
    'src/Makefile.in': '__d_libdir__libdmtcp_so_SOURCES = ' + ' '.join(PID_MAKE_SOURCES) + '\n' +
                       'am___d_libdir__libdmtcp_so_OBJECTS = ' + ' '.join(PID_OBJECTS) + '\n' +
                       'am__depfiles_remade = ' + ' '.join(PID_DEPFILES) + '\n',
    'src/Makefile': '__d_libdir__libdmtcp_so_SOURCES = ' + ' '.join(PID_MAKE_SOURCES) + '\n' +
                    'am___d_libdir__libdmtcp_so_OBJECTS = ' + ' '.join(PID_OBJECTS) + '\n' +
                    'am__depfiles_remade = ' + ' '.join(PID_DEPFILES) + '\n',
    'src/plugin/Makefile.am': 'no pid dso here\n',
    'src/plugin/Makefile.in': 'no pid dso here\n',
    'src/plugin/Makefile': 'no pid dso here\n',
    'src/dmtcp_launch.cpp': 'static const char *libs = "libdmtcp.so";\n',
    'src/plugin/pid/pid.cpp': 'pid_t virtualTid = VirtualPidTable::gettid();\ndmtcp_pthread_set_tid(pthread_self(), virtualTid);\nstatic vector<pid_t> *exitedChildTids = NULL;\nstatic DmtcpMutex exitedChildTidsLock = DMTCP_MUTEX_INITIALIZER_LLL;\nDmtcpPluginDescriptor_t *\ndmtcp_Pid_PluginDescr() { return &pidPlugin; }\n',
    'src/threadlist.cpp': 'motherpid = (pid_t)_real_syscall(SYS_getpid);\nmotherpid = (pid_t)_real_syscall(SYS_getpid);\nTHREAD_TGKILL(motherpid, thread->tid, sig);\nTLSInfo_SaveTLSState(curThread);\nTLSInfo_RestoreTLSTidPid(motherofall);\n_real_syscall(SYS_kill, motherpid, i);\nthread->tid = (pid_t)_real_syscall(SYS_gettid);\n',
    'src/pluginmanager.cpp': 'dmtcp_register_plugin(dmtcp_Core_PluginDescr());\ndmtcp_register_plugin(dmtcp_Pid_PluginDescr());\n',
    'src/syscallsreal.c': 'int _real_fork(void) { return 0; }\n',
    'src/syscallwrappers.h': '',
    'src/plugin/pid/pid_syscallsreal.c': 'int _real_getpid(void) { return 1; }\n',
    'src/execwrappers.cpp': 'extern "C" pid_t\nfork() { return 0; }\nextern "C" pid_t\nvfork() { return 0; }\n',
    'src/plugin/pid/pid_miscwrappers.cpp': 'static void helper() {}\n',
    'src/miscwrappers.cpp': 'dmtcp_virtual_to_real_pid dmtcp_real_to_virtual_pid dmtcp_pid_erase_virtual_pid WNOHANG _real_wait4 _real_waitid SYS_gettid SYS_tkill SYS_tgkill SYS_getpid SYS_getppid SYS_getpgid SYS_setpgid SYS_getsid SYS_kill SYS_waitid SYS_wait4\nextern "C" pid_t\nwait(int *s) { return 0; }\nextern "C" pid_t\nwaitpid(pid_t p, int *s, int o) { return 0; }\nextern "C" int\nwaitid(idtype_t t, id_t i, siginfo_t *s, int o) { return 0; }\nextern "C" pid_t\nwait3(int *s, int o, struct rusage *r) { return 0; }\nextern "C" pid_t\nwait4(pid_t p, int *s, int o, struct rusage *r) { return 0; }\nextern "C" long\nsyscall(long n) { return 0; }\n',
    'src/wrappers.cpp': 'F_SETOWN F_GETOWN dmtcp_virtual_to_real_pid dmtcp_real_to_virtual_pid processDupFd F_DUPFD F_DUPFD_CLOEXEC\nextern "C" int\nfcntl(int fd, int cmd, ...) { return 0; }\n',
    'src/plugin/pid/pidwrappers.cpp': 'static void helper() {}\n',
    'src/plugin/timer/timerwrappers.cpp': 'extern "C" int\ntimer_create(clockid_t c, struct sigevent *s, timer_t *t) { return 0; }\nextern "C" int\nclock_getcpuclockid(pid_t p, clockid_t *c) { return 0; }\nextern "C" int\npthread_getcpuclockid(pthread_t t, clockid_t *c) { return 0; }\n',
    'src/plugin/svipc/sysvipcwrappers.cpp': 'int shmctl(int a, int b, struct shmid_ds *c) { return 0; }\nint msgctl(int a, int b, struct msqid_ds *c) { return 0; }\nint semctl(int a, int b, int c, ...) { return 0; }\n',
    'src/plugin/svipc/sysvipc.cpp': 'pid_t sysvipcRealPid() { return dmtcp_virtual_to_real_pid(getpid()); }\nif (info.shm_lpid == sysvipcRealPid()) {}\nif (sysvipcRealPid() == _real_semctl(_realId, 0, GETPID)) {}\nif (buf.msg_lspid == sysvipcRealPid()) {}\n',
    'src/plugin/ipc/file/posixipcwrappers.cpp': 'int mq_notify(mqd_t m, const struct sigevent *s) { return 0; }\n',
    'src/plugin/timer/timerwrappers.h': '',
    'src/plugin/svipc/sysvipcwrappers.h': '',
    'src/plugin/ipc/file/filewrappers.h': '',
    'src/plugin/pid/glibc_pthread.cpp': 'int glibcMinorVersion() { return 39; }\nvoid glibc_safe() { static int libcMinor = glibcMinorVersion(); }\n',
    'src/plugin/pid/glibc_pthread.h': '',
    'src/plugin/pid/pid_filewrappers.cpp': '',
    'src/plugin/pid/pid.h': '',
    'src/plugin/pid/pidwrappers.h': '',
    'src/plugin/pid/sched_wrappers.cpp': '',
    'src/plugin/pid/virtualpidtable.cpp': '',
    'src/plugin/pid/virtualpidtable.h': '',
    'test/Makefile.in': 'pid-built-in-preload: pid-built-in-preload.c\n\t$(CC) -o $@ $< $(CFLAGS)\n',
    'test/Makefile': 'pid-built-in-preload: pid-built-in-preload.c\n\t$(CC) -o $@ $< $(CFLAGS)\n',
    PROBE_SOURCE: 'libdmtcp_pid.so DMTCP_HIJACK_LIBS DMTCP_PID_BUILT_IN_PRELOAD_AFTER_EXEC fork() waitpid\n',
  }
  ok = Result(emit=False)
  reader = Reader(fixtures)
  validate_static(ok, reader)
  validate_helpers(ok, reader)
  validate_single_owner(ok, reader, 'fork-owner', FORK_SYMBOLS, FORK_OWNER_FILES)
  validate_fork_core_identity(ok, reader)
  validate_single_owner(ok, reader, 'wrapper-owner', WRAPPER_SYMBOLS, WRAPPER_OWNER_FILES)
  validate_wrapper_composition(ok, reader)
  validate_single_owner(ok, reader, 'overlap-owner', OVERLAP_SYMBOLS, OVERLAP_OWNER_FILES)
  validate_overlap_composition(ok, reader)
  validate_test_wiring(ok, reader)
  if ok.failures:
    print('FAIL self-test: positive fixture unexpectedly failed')
    for contract, msg, details in ok.failures:
      print('  - {0}: {1}: {2}'.format(contract, msg, '; '.join(details)))
    return 1

  bad_fixtures = dict(fixtures)
  bad_fixtures['src/plugin/pid/pid_miscwrappers.cpp'] = 'extern "C" pid_t\nfork() { return 1; }\n'
  bad = Result(emit=False)
  validate_single_owner(bad, Reader(bad_fixtures), 'fork-owner', ('fork',), FORK_OWNER_FILES)
  if not bad.failures:
    print('FAIL self-test: duplicate fork owner fixture was not rejected')
    return 1

  bad_fixtures = dict(fixtures)
  bad_fixtures['src/dmtcp_launch.cpp'] = '"libdmtcp_pid.so"\n'
  bad = Result(emit=False)
  validate_static(bad, Reader(bad_fixtures))
  if not any(contract == 'stale-pid-dso-source' for contract, _msg, _details in bad.failures):
    print('FAIL self-test: stale dmtcp_launch PID DSO fixture was not rejected')
    return 1

  bad_fixtures = dict(fixtures)
  bad_fixtures['src/plugin/pid/pid.cpp'] += 'static string pidMapFile;\n'
  bad = Result(emit=False)
  validate_static(bad, Reader(bad_fixtures))
  if not any(contract == 'pid-static-initialization' for contract, _msg, _details in bad.failures):
    print('FAIL self-test: file-scope static string fixture was not rejected')
    return 1

  bad_fixtures = dict(fixtures)
  bad_fixtures['src/plugin/pid/virtualpidtable.cpp'] = 'std::map<int, int> pidCache;\n'
  bad = Result(emit=False)
  validate_static(bad, Reader(bad_fixtures))
  if not any(contract == 'pid-static-initialization' for contract, _msg, _details in bad.failures):
    print('FAIL self-test: file-scope STL container fixture was not rejected')
    return 1

  hidden = Result(emit=False)
  Reader(fixtures).read_static('.gsd/pid-hidden.cpp', hidden, 'pid-static-initialization')
  if not any(contract == 'static-input-scope' for contract, _msg, _details in hidden.failures):
    print('FAIL self-test: hidden/unapproved static input fixture was not rejected')
    return 1

  bad_fixtures = dict(fixtures)
  bad_fixtures['src/plugin/pid/pid_syscallsreal.c'] = 'int _real_fork(void) { return 2; }\n'
  bad = Result(emit=False)
  validate_helpers(bad, Reader(bad_fixtures))
  if not any(contract == 'real-helper-ownership' for contract, _msg, _details in bad.failures):
    print('FAIL self-test: duplicate _real_fork fixture was not rejected')
    return 1

  bad_fixtures = dict(fixtures)
  bad_fixtures['src/threadlist.cpp'] = 'motherpid = getpid();\nTHREAD_TGKILL(motherpid, thread->tid, sig);\nTLSInfo_SaveTLSState(curThread);\nTLSInfo_RestoreTLSTidPid(motherofall);\n'
  bad = Result(emit=False)
  validate_fork_core_identity(bad, Reader(bad_fixtures))
  if not any(contract == 'fork-core-real-pid' for contract, _msg, _details in bad.failures):
    print('FAIL self-test: virtual motherpid fixture was not rejected')
    return 1

  bad_fixtures = dict(fixtures)
  bad_fixtures['src/plugin/svipc/sysvipc.cpp'] = 'if (info.shm_lpid == getpid()) {}\nif (getpid() == _real_semctl(_realId, 0, GETPID)) {}\nif (buf.msg_lspid == getpid()) {}\n'
  bad = Result(emit=False)
  validate_overlap_composition(bad, Reader(bad_fixtures))
  if not any(contract == 'overlap-composition' for contract, _msg, _details in bad.failures):
    print('FAIL self-test: SysV virtual getpid leader fixture was not rejected')
    return 1

  print('PASS self-test: PID built-in gate rejects duplicate wrapper, stale DSO, static-init, helper-collision, hidden-path, and SysV virtual-leader fixtures')
  return 0

def main(argv):
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument('mode', choices=('static', 'artifacts', 'helpers', 'fork', 'wrappers', 'overlap', 'runtime', 'self-test', 'full'))
  args = parser.parse_args(argv)

  if args.mode == 'self-test':
    return self_test()

  result = Result()
  reader = Reader()

  if args.mode in ('static', 'full'):
    validate_static(result, reader)
    validate_test_wiring(result, reader)
  if args.mode in ('helpers', 'full'):
    validate_helpers(result, reader)
  if args.mode in ('fork', 'full'):
    validate_single_owner(result, reader, 'fork-owner', FORK_SYMBOLS, FORK_OWNER_FILES)
    validate_fork_core_identity(result, reader)
  if args.mode in ('wrappers', 'full'):
    validate_single_owner(result, reader, 'wrapper-owner', WRAPPER_SYMBOLS, WRAPPER_OWNER_FILES)
    validate_wrapper_composition(result, reader)
  if args.mode in ('overlap', 'full'):
    validate_single_owner(result, reader, 'overlap-owner', OVERLAP_SYMBOLS, OVERLAP_OWNER_FILES)
    validate_overlap_composition(result, reader)
  if args.mode in ('artifacts', 'full'):
    validate_artifacts(result)
  if args.mode in ('runtime', 'full'):
    validate_runtime(result)

  return result.finish()

if __name__ == '__main__':
  sys.exit(main(sys.argv[1:]))
