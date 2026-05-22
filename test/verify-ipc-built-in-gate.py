#!/usr/bin/env python3
"""Executable gate for moving the IPC internal plugin into libdmtcp.so.

Modes:
  static     source/config-only checks; never walks hidden planning/audit paths
  artifacts  built-output stale libdmtcp_ipc.so checks
  overlap    documented PID/IPC mq_notify overlap only
  self-test  negative inline fixtures for the gate itself
  full       all checks, plus runtime probe when built artifacts exist
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

IPC_SOURCES = (
  'src/plugin/ipc/connection.cpp', 'src/plugin/ipc/connection.h',
  'src/plugin/ipc/connectionidentifier.cpp', 'src/plugin/ipc/connectionidentifier.h',
  'src/plugin/ipc/connectionlist.cpp', 'src/plugin/ipc/connectionlist.h',
  'src/plugin/ipc/ipc.cpp', 'src/plugin/ipc/ipc.h',
  'src/plugin/ipc/event/eventconnection.cpp', 'src/plugin/ipc/event/eventconnection.h',
  'src/plugin/ipc/event/eventconnlist.cpp', 'src/plugin/ipc/event/eventconnlist.h',
  'src/plugin/ipc/event/eventwrappers.cpp', 'src/plugin/ipc/event/eventwrappers.h',
  'src/plugin/ipc/event/util_descriptor.cpp', 'src/plugin/ipc/event/util_descriptor.h',
  'src/plugin/ipc/file/fileconnection.cpp', 'src/plugin/ipc/file/fileconnection.h',
  'src/plugin/ipc/file/fileconnlist.cpp', 'src/plugin/ipc/file/fileconnlist.h',
  'src/plugin/ipc/file/filewrappers.cpp', 'src/plugin/ipc/file/filewrappers.h',
  'src/plugin/ipc/file/openwrappers.cpp', 'src/plugin/ipc/file/posixipcwrappers.cpp',
  'src/plugin/ipc/file/ptyconnection.cpp', 'src/plugin/ipc/file/ptyconnection.h',
  'src/plugin/ipc/file/ptyconnlist.cpp', 'src/plugin/ipc/file/ptyconnlist.h',
  'src/plugin/ipc/file/ptywrappers.cpp', 'src/plugin/ipc/file/ptywrappers.h',
  'src/plugin/ipc/socket/connectionmessage.h',
  'src/plugin/ipc/socket/connectionrewirer.cpp', 'src/plugin/ipc/socket/connectionrewirer.h',
  'src/plugin/ipc/socket/kernelbufferdrainer.cpp', 'src/plugin/ipc/socket/kernelbufferdrainer.h',
  'src/plugin/ipc/socket/socketconnection.cpp', 'src/plugin/ipc/socket/socketconnection.h',
  'src/plugin/ipc/socket/socketconnlist.cpp', 'src/plugin/ipc/socket/socketconnlist.h',
  'src/plugin/ipc/socket/socketwrappers.cpp', 'src/plugin/ipc/socket/socketwrappers.h',
  'src/plugin/ipc/ssh/ssh.cpp', 'src/plugin/ipc/ssh/sshdrainer.cpp',
  'src/plugin/ipc/ssh/sshdrainer.h', 'src/plugin/ipc/ssh/ssh.h',
)
STATIC_INPUTS = (
  'src/Makefile.am', 'src/Makefile.in', 'src/Makefile',
  'src/plugin/Makefile.am', 'src/plugin/Makefile.in', 'src/plugin/Makefile',
  'src/dmtcp_launch.cpp', 'src/execwrappers.cpp', 'src/util_exec.cpp',
  'src/pluginmanager.cpp',
) + IPC_SOURCES
IPC_MAKE_SOURCES = tuple(p.replace('src/', '', 1) for p in IPC_SOURCES)
IPC_CPP_SOURCES = tuple(p for p in IPC_MAKE_SOURCES if p.endswith('.cpp'))
IPC_OBJECTS = tuple(p.replace('.cpp', '.$(OBJEXT)') for p in IPC_CPP_SOURCES)
IPC_DEPFILES = tuple(p.rsplit('/', 1)[0] + '/$(DEPDIR)/' + p.rsplit('/', 1)[1].replace('.cpp', '.Po') for p in IPC_CPP_SOURCES)
FORBIDDEN_DSO = ('libdmtcp_ipc.so', 'libdmtcp_ipc')
FINAL_PRELOAD_LIB = 'libdmtcp.so'
OPTIONAL_PRELOAD_ROWS = ('libdmtcp_modify-env.so', 'libdmtcp_unique-ckpt.so', 'libdmtcp_pathvirt.so')
REMOVED_INTERNAL_DSO_ROWS = (
  ('timer', 'libdmtcp_timer'),
  ('SysV IPC', 'libdmtcp_svipc'),
  ('IPC', 'libdmtcp_ipc'),
  ('PID', 'libdmtcp_pid'),
  ('alloc', 'libdmtcp_alloc'),
  ('DL', 'libdmtcp_dl'),
)
NO_DSO_PATHS = ('src/plugin/Makefile.am', 'src/plugin/Makefile.in', 'src/plugin/Makefile', 'src/dmtcp_launch.cpp', 'src/execwrappers.cpp', 'src/util_exec.cpp')
HELPERS = ('libssh.a', 'libssh_a_SOURCES', 'ipc/ssh/util_ssh.cpp', 'ipc/ssh/util_ssh.h', 'dmtcp_ssh', 'ipc/ssh/dmtcp_ssh.cpp', 'dmtcp_sshd', 'ipc/ssh/dmtcp_sshd.cpp')
DESCRIPTOR_ORDER = ('ssh', 'event', 'file', 'pty', 'socket')
PROBE = 'test/ipc-built-in-preload'
PROBE_SOURCE = 'test/ipc-built-in-preload.c'
PID_OVERLAP_ALLOWED = ('src/plugin/pid/pid_miscwrappers.cpp', 'src/plugin/pid/pidwrappers.h', 'src/plugin/pid/pid_syscallsreal.c')
IPC_ENABLE_HELPER = 'dmtcp_ipc_wrappers_enabled'
IPC_WRAPPER_FAST_PASS = (
  ('src/plugin/ipc/socket/socketwrappers.cpp', 'socket', '_real_socket'),
  ('src/plugin/ipc/socket/socketwrappers.cpp', 'connect', '_real_connect'),
  ('src/plugin/ipc/socket/socketwrappers.cpp', 'bind', '_real_bind'),
  ('src/plugin/ipc/socket/socketwrappers.cpp', 'listen', '_real_listen'),
  ('src/plugin/ipc/socket/socketwrappers.cpp', 'accept', '_real_accept'),
  ('src/plugin/ipc/socket/socketwrappers.cpp', 'accept4', '_real_accept4'),
  ('src/plugin/ipc/socket/socketwrappers.cpp', 'setsockopt', '_real_setsockopt'),
  ('src/plugin/ipc/socket/socketwrappers.cpp', 'socketpair', '_real_socketpair'),
  ('src/plugin/ipc/socket/socketwrappers.cpp', 'getaddrinfo', '_real_getaddrinfo'),
  ('src/plugin/ipc/socket/socketwrappers.cpp', 'getnameinfo', '_real_getnameinfo'),
  ('src/plugin/ipc/socket/socketwrappers.cpp', 'gethostbyname', '_real_gethostbyname'),
  ('src/plugin/ipc/socket/socketwrappers.cpp', 'gethostbyaddr', '_real_gethostbyaddr'),
  ('src/plugin/ipc/event/eventwrappers.cpp', 'poll', '_real_poll'),
  ('src/plugin/ipc/event/eventwrappers.cpp', '__poll_chk', '_real_poll_chk'),
  ('src/plugin/ipc/event/eventwrappers.cpp', 'pselect', '_real_pselect'),
  ('src/plugin/ipc/event/eventwrappers.cpp', 'select', '_real_select'),
  ('src/plugin/ipc/event/eventwrappers.cpp', 'signalfd', '_real_signalfd'),
  ('src/plugin/ipc/event/eventwrappers.cpp', 'eventfd', '_real_eventfd'),
  ('src/plugin/ipc/event/eventwrappers.cpp', 'epoll_create', '_real_epoll_create'),
  ('src/plugin/ipc/event/eventwrappers.cpp', 'epoll_create1', '_real_epoll_create1'),
  ('src/plugin/ipc/event/eventwrappers.cpp', 'epoll_ctl', '_real_epoll_ctl'),
  ('src/plugin/ipc/event/eventwrappers.cpp', 'epoll_wait', '_real_epoll_wait'),
  ('src/plugin/ipc/event/eventwrappers.cpp', 'inotify_init', '_real_inotify_init'),
  ('src/plugin/ipc/event/eventwrappers.cpp', 'inotify_init1', '_real_inotify_init1'),
  ('src/plugin/ipc/event/eventwrappers.cpp', 'inotify_add_watch', '_real_inotify_add_watch'),
  ('src/plugin/ipc/event/eventwrappers.cpp', 'inotify_rm_watch', '_real_inotify_rm_watch'),
  ('src/plugin/ipc/file/posixipcwrappers.cpp', 'mq_open', '_real_mq_open'),
  ('src/plugin/ipc/file/posixipcwrappers.cpp', 'mq_close', '_real_mq_close'),
  ('src/plugin/ipc/file/posixipcwrappers.cpp', 'mq_notify', '_real_mq_notify'),
  ('src/plugin/ipc/file/posixipcwrappers.cpp', 'mq_send', '_real_mq_send'),
  ('src/plugin/ipc/file/posixipcwrappers.cpp', 'mq_receive', '_real_mq_receive'),
  ('src/plugin/ipc/file/posixipcwrappers.cpp', 'mq_timedsend', '_real_mq_timedsend'),
  ('src/plugin/ipc/file/posixipcwrappers.cpp', 'mq_timedreceive', '_real_mq_timedreceive'),
)
IPC_VOID_FAST_PASS = (
  ('src/plugin/ipc/event/eventconnlist.cpp', 'dmtcp_EventConnList_EventHook'),
  ('src/plugin/ipc/file/fileconnlist.cpp', 'dmtcp_FileConnList_EventHook'),
  ('src/plugin/ipc/file/ptyconnlist.cpp', 'dmtcp_PtyConnList_EventHook'),
  ('src/plugin/ipc/socket/socketconnlist.cpp', 'dmtcp_SocketConnList_EventHook'),
  ('src/plugin/ipc/ssh/ssh.cpp', 'dmtcp_SSH_EventHook'),
  ('src/plugin/ipc/ssh/ssh.cpp', 'dmtcp_ssh_register_fds'),
)
NON_POD_STATIC_TYPES = ('string', 'vector', 'map', 'set', 'list', 'deque', 'unordered_map', 'unordered_set')

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
      print('FAIL summary: {0} contract(s) failed; {1} contract(s) passed.'.format(len(self.failures), self.passes))
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

def strip_comments(text):
  def repl(m):
    return ''.join('\n' if ch == '\n' else ' ' for ch in m.group(0))
  text = re.sub(r'/\*.*?\*/', repl, text, flags=re.S)
  return re.sub(r'//.*', '', text)

def line_hits(text, needle):
  return ['line {0}: {1}'.format(i, line.strip()) for i, line in enumerate(text.splitlines(), 1) if needle in line]

def make_var(text, name):
  lines = text.splitlines()
  for i, line in enumerate(lines):
    if line.startswith(name + ' ='):
      block = [line]
      while block[-1].rstrip().endswith('\\') and i + 1 < len(lines):
        i += 1; block.append(lines[i])
      return '\n'.join(block)
  return ''

def braced_body(text, open_idx):
  depth = 0
  for i in range(open_idx, len(text)):
    if text[i] == '{': depth += 1
    elif text[i] == '}':
      depth -= 1
      if depth == 0: return text[open_idx + 1:i]
  return None

def function_body(text, pattern):
  for m in re.finditer(pattern, text, flags=re.S | re.M):
    brace = text.find('{', m.end())
    semicolon = text.find(';', m.end())
    if brace != -1 and not (semicolon != -1 and semicolon < brace):
      body = braced_body(text, brace)
      if body is not None: return body
  return None

def contracts(result):
  return set(c for c, _m, _d in result.failures)

def require_items(result, reader, rel, var, items, contract):
  block = make_var(reader.read_static(rel, result, contract), var)
  if not block:
    result.fail(contract, 'Makefile variable is missing', ['{0}: {1}'.format(rel, var)]); return
  missing = [x for x in items if x not in block]
  if missing:
    result.fail(contract, 'Makefile variable is missing IPC built-in entries', ['{0}: missing {1}'.format(rel, x) for x in missing]); return
  result.pass_(contract, '{0}:{1} contains {2} IPC entries'.format(rel, var, len(items)))

def final_preload_state_details(body):
  libs = re.findall(r'"([^"]+)"', body)
  details = []
  for label, needle in REMOVED_INTERNAL_DSO_ROWS:
    stale = [lib for lib in libs if needle in lib]
    if stale:
      details.append('final pluginInfo must not contain removed {0} DSO row(s): {1}'.format(label, ', '.join(stale)))
  if FINAL_PRELOAD_LIB not in libs:
    details.append('final pluginInfo table is missing {0}'.format(FINAL_PRELOAD_LIB))
  elif libs.count(FINAL_PRELOAD_LIB) != 1:
    details.append('final pluginInfo table must contain exactly one {0} row'.format(FINAL_PRELOAD_LIB))
  else:
    final_idx = libs.index(FINAL_PRELOAD_LIB)
    trailing = libs[final_idx + 1:]
    if trailing:
      details.append('{0} must remain the final DMTCP-owned preload row; trailing row(s): {1}'.format(FINAL_PRELOAD_LIB, ', '.join(trailing)))
    misplaced_optional = [lib for lib in OPTIONAL_PRELOAD_ROWS if lib in libs and libs.index(lib) > final_idx]
    if misplaced_optional:
      details.append('optional launcher row(s) must appear before {0}: {1}'.format(FINAL_PRELOAD_LIB, ', '.join(misplaced_optional)))
  return details, libs


def validate_final_launch_preload_state(result, text, contract):
  m = re.search(r'static\s+struct\s+PluginInfo\s+pluginInfo\[\]\s*=\s*\{(?P<body>.*?)^\};', text, flags=re.S | re.M)
  if not m:
    result.fail(contract, 'pluginInfo preload table is missing'); return
  details, _libs = final_preload_state_details(m.group('body'))
  setld = function_body(text, r'static\s+void\s+setLDPreloadLibs\s*\(')
  if setld is None: details.append('setLDPreloadLibs body is missing')
  else:
    user_idx = setld.find('preloadLibs += getenv(ENV_VAR_PLUGIN)')
    loop_idx = setld.find('for (size_t i = 0; i < numLibs; i++)')
    if user_idx == -1 or loop_idx == -1 or user_idx > loop_idx: details.append('user ENV_VAR_PLUGIN preload entries are not prepended before DMTCP-owned rows')
    if 'preloadLibs = Util::getPath("libdmtcp.so")' not in setld: details.append('disable-all branch no longer reduces to libdmtcp.so')
    for label, needle in REMOVED_INTERNAL_DSO_ROWS:
      if needle in setld: details.append('setLDPreloadLibs body contains removed {0} DSO name {1}'.format(label, needle))
  if details: result.fail(contract, 'final launch preload state contract is broken', details)
  else: result.pass_(contract, 'user plugins precede optional DMTCP rows, libdmtcp.so is final, and removed timer/SysV IPC/IPC/PID/alloc/DL DSO rows are absent')


def validate_static_scope(result, reader, inputs=STATIC_INPUTS):
  details = []
  seen = set()
  for rel in inputs:
    p = Path(rel); parts = p.parts
    if rel in seen: details.append('duplicate static input: {0}'.format(rel))
    seen.add(rel)
    if p.is_absolute() or '..' in parts: details.append('non-repository-relative static input: {0}'.format(rel))
    if not parts or parts[0] != 'src': details.append('static input is not source/configuration scoped: {0}'.format(rel))
    if any(part in HIDDEN for part in parts): details.append('static input uses forbidden hidden state: {0}'.format(rel))
    if p.suffix not in ('', '.am', '.cpp', '.h', '.in'): details.append('static input suffix is not source/configuration-like: {0}'.format(rel))
    if reader.fixtures is None and not (REPO_ROOT / rel).is_file(): details.append('static input does not exist: {0}'.format(rel))
    if reader.fixtures is not None and rel not in reader.fixtures: details.append('static fixture is missing: {0}'.format(rel))
  if details: result.fail('static-input-scope', 'static input list violates the source-only contract', details)
  else: result.pass_('static-input-scope', 'static mode is limited to explicit src/ source/config paths and excludes hidden planning/audit paths')

def validate_makefiles(result, reader):
  for rel in ('src/Makefile.am', 'src/Makefile.in', 'src/Makefile'):
    require_items(result, reader, rel, '__d_libdir__libdmtcp_so_SOURCES', IPC_MAKE_SOURCES, 'makefile-libdmtcp-ipc-sources')
  for rel in ('src/Makefile.in', 'src/Makefile'):
    require_items(result, reader, rel, 'am___d_libdir__libdmtcp_so_OBJECTS', IPC_OBJECTS, 'makefile-libdmtcp-ipc-objects')
    require_items(result, reader, rel, 'am__depfiles_remade', IPC_DEPFILES, 'makefile-libdmtcp-ipc-dependencies')

def validate_no_dso(result, reader):
  bad = []
  for rel in NO_DSO_PATHS:
    text = reader.read_static(rel, result, 'no-libdmtcp-ipc-dso')
    for needle in FORBIDDEN_DSO:
      bad.extend('{0}: {1}'.format(rel, hit) for hit in line_hits(text, needle))
  if bad: result.fail('no-libdmtcp-ipc-dso', 'removed IPC DSO is still constructed or referenced', bad)
  else: result.pass_('no-libdmtcp-ipc-dso', 'plugin Makefiles plus launch/exec preload code contain no libdmtcp_ipc.so reference')

def validate_helpers(result, reader):
  bad = []
  for rel in ('src/plugin/Makefile.am', 'src/plugin/Makefile.in', 'src/plugin/Makefile'):
    text = reader.read_static(rel, result, 'ipc-helper-preservation')
    bad.extend('{0}: missing {1}'.format(rel, t) for t in HELPERS if t not in text)
  if bad: result.fail('ipc-helper-preservation', 'IPC SSH helper artifacts are not preserved', bad)
  else: result.pass_('ipc-helper-preservation', 'libssh.a, dmtcp_ssh, and dmtcp_sshd remain helper artifacts')

def validate_helper_header_boundary(result, reader):
  text = reader.read_static('src/plugin/ipc/ipc.h', result, 'ipc-helper-header-boundary')
  if '#include "constants.h"' in text:
    result.fail(
      'ipc-helper-header-boundary',
      'ipc.h depends on src/constants.h, which is not in the src/plugin helper include path',
      ['src/plugin/ipc/ipc.h is included by ipc/ssh helper binaries; keep the disable-all env token helper-visible or update helper include paths']
    )
  else:
    result.pass_('ipc-helper-header-boundary', 'ipc.h remains consumable by src/plugin helper builds without a src/constants.h include')

def validate_launch(result, reader):
  text = reader.read_static('src/dmtcp_launch.cpp', result, 'launch-preload-state')
  validate_final_launch_preload_state(result, text, 'launch-preload-state')

def kind(name):
  lower = name.lower()
  for k in DESCRIPTOR_ORDER:
    if k in lower: return k
  return None

def validate_pluginmanager(result, reader):
  text = strip_comments(reader.read_static('src/pluginmanager.cpp', result, 'pluginmanager-ipc-descriptors'))
  details = []
  decls = re.findall(r'DmtcpPluginDescriptor_t\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(\s*\)\s*;', text)
  names = {}
  decls_by_kind = {}
  for name in decls:
    k = kind(name)
    if k:
      decls_by_kind.setdefault(k, []).append(name)
      if k not in names: names[k] = name
  for k, kind_decls in sorted(decls_by_kind.items()):
    if len(kind_decls) > 1:
      details.append('duplicate IPC {0} descriptor accessor declaration(s): {1}'.format(k, ', '.join(kind_decls)))
  for k in DESCRIPTOR_ORDER:
    if k not in names: details.append('missing IPC {0} descriptor accessor declaration'.format(k))
  helper_name = None
  for helper in re.finditer(r'static\s+bool\s+([A-Za-z_][A-Za-z0-9_]*PluginEnabled)\s*\(\s*\)', text):
    candidate = helper.group(1)
    lower = candidate.lower()
    if 'ipc' in lower and 'svipc' not in lower:
      helper_name = candidate
      break
  if not helper_name: details.append('missing IPC disable-all helper')
  else:
    hb = function_body(text, r'static\s+bool\s+' + re.escape(helper_name) + r'\s*\(\s*\)') or ''
    if 'getenv(ENV_VAR_DISABLE_ALL_PLUGINS)' not in hb: details.append('IPC helper does not read ENV_VAR_DISABLE_ALL_PLUGINS')
    if 'strcmp(disableAllPlugins, "1") != 0' not in hb: details.append('IPC helper does not preserve value 1 disable-all semantics')
  init = function_body(text, r'extern\s+"C"\s+void\s+dmtcp_initialize_plugin\s*\(')
  if init is None: details.append('libdmtcp dmtcp_initialize_plugin() is missing')
  else:
    if 'NEXT_FNC(dmtcp_initialize_plugin)' not in init: details.append('dmtcp_initialize_plugin does not preserve NEXT_FNC chain')
    if helper_name and helper_name not in init: details.append('IPC descriptors are not gated by {0}()'.format(helper_name))
    if len(names) == len(DESCRIPTOR_ORDER):
      positions = [init.find(names[k]) for k in DESCRIPTOR_ORDER]
      if any(p == -1 for p in positions): details.append('not all IPC descriptors appear in dmtcp_initialize_plugin')
      elif positions != sorted(positions): details.append('IPC descriptor rows are not in ssh, event, file, pty, socket order')
      for k in DESCRIPTOR_ORDER:
        count = len(re.findall(r'\b' + re.escape(names[k]) + r'\s*\(', init))
        if count != 1:
          details.append('IPC {0} descriptor must be registered exactly once in dmtcp_initialize_plugin; observed {1}'.format(k, count))
  if details: result.fail('pluginmanager-ipc-descriptors', 'built-in IPC descriptor registration contract is broken', details)
  else: result.pass_('pluginmanager-ipc-descriptors', 'pluginmanager gates IPC descriptors in ssh,event,file,pty,socket order and preserves NEXT_FNC')

def validate_no_nested(result, reader):
  bad = []
  pat = re.compile(r'(?:extern\s+"C"\s+|EXTERNC\s+)?void\s+dmtcp_initialize_plugin\s*\(', flags=re.S)
  for rel in IPC_SOURCES:
    text = strip_comments(reader.read_static(rel, result, 'ipc-no-nested-initializer'))
    bad.extend('{0}: {1}'.format(rel, hit) for hit in line_hits(text, 'DMTCP_DECL_PLUGIN'))
    if pat.search(text): bad.append('{0}: defines dmtcp_initialize_plugin()'.format(rel))
    if 'NEXT_FNC(dmtcp_initialize_plugin)' in text: bad.append('{0}: chains nested dmtcp_initialize_plugin()'.format(rel))
  if bad: result.fail('ipc-no-nested-initializer', 'IPC sources must not self-register as a nested plugin', bad)
  else: result.pass_('ipc-no-nested-initializer', 'IPC sources contain no DMTCP_DECL_PLUGIN or IPC-local dmtcp_initialize_plugin')

def validate_abi(result, reader):
  text = strip_comments(reader.read_static('src/pluginmanager.cpp', result, 'external-plugin-abi'))
  body = function_body(text, r'extern\s+"C"\s+void\s+dmtcp_initialize_plugin\s*\(')
  details = []
  if body is None: details.append('missing extern C dmtcp_initialize_plugin')
  else:
    if 'NEXT_FNC(dmtcp_initialize_plugin)' not in body: details.append('missing NEXT_FNC initializer chain')
    if not re.search(r'\(\s*\*\s*fn\s*\)\s*\(\s*\)', body): details.append('NEXT_FNC result is not invoked')
  if details: result.fail('external-plugin-abi', 'external plugin ABI chain changed', details)
  else: result.pass_('external-plugin-abi', 'libdmtcp keeps extern C dmtcp_initialize_plugin and invokes NEXT_FNC')

def validate_ipc_enable_helper(result, reader):
  text = strip_comments(reader.read_static('src/plugin/ipc/ipc.h', result, 'ipc-disable-all-helper'))
  body = function_body(text, r'static\s+inline\s+bool\s+' + re.escape(IPC_ENABLE_HELPER) + r'\s*\(\s*\)')
  details = []
  if body is None:
    details.append('missing {0}() helper in ipc.h'.format(IPC_ENABLE_HELPER))
  else:
    if 'getenv(ENV_VAR_DISABLE_ALL_PLUGINS)' not in body:
      details.append('{0}() does not read ENV_VAR_DISABLE_ALL_PLUGINS at point of use'.format(IPC_ENABLE_HELPER))
    if 'strcmp(disableAllPlugins, "1") != 0' not in body:
      details.append('{0}() does not preserve value 1 disable-all semantics'.format(IPC_ENABLE_HELPER))
  if details: result.fail('ipc-disable-all-helper', 'IPC disable-all helper contract is broken', details)
  else: result.pass_('ipc-disable-all-helper', 'ipc.h exposes a POD-only point-of-use disable-all helper')

def validate_disable_all_fast_pass(result, reader):
  details = []
  seen = set()
  for rel, func, real in IPC_WRAPPER_FAST_PASS:
    text = strip_comments(reader.read_static(rel, result, 'ipc-disable-all-fast-pass'))
    body = function_body(text, r'(?:extern\s+"C"|EXTERNC)\s+[^;{]*\b' + re.escape(func) + r'\s*\(')
    label = '{0}:{1}'.format(rel, func)
    if body is None:
      details.append(label + ' wrapper body is missing')
      continue
    compact = re.sub(r'\s+', ' ', body)
    fast = r'if\s*\(\s*!\s*' + re.escape(IPC_ENABLE_HELPER) + r'\s*\(\s*\)\s*\)\s*\{\s*return\s+' + re.escape(real) + r'\s*\('
    if not re.search(fast, compact):
      details.append(label + ' lacks a direct disable-all return to ' + real)
    helper_idx = body.find(IPC_ENABLE_HELPER)
    mutation_points = [p for p in (body.find('DMTCP_PLUGIN_DISABLE_CKPT'), body.find('ConnList::instance'), body.find('dmtcp_get_generation'), body.find('JALLOC_HELPER_MALLOC'), body.find('new ')) if p != -1]
    if helper_idx == -1:
      details.append(label + ' does not call ' + IPC_ENABLE_HELPER + '()')
    elif mutation_points and min(mutation_points) < helper_idx:
      details.append(label + ' can reach DMTCP mutation/restart logic before checking disable-all')
    seen.add((rel, func))
  for rel, func in IPC_VOID_FAST_PASS:
    text = strip_comments(reader.read_static(rel, result, 'ipc-disable-all-fast-pass'))
    body = function_body(text, r'(?:extern\s+"C"\s+)?(?:void|EXTERNC\s+void)\s+' + re.escape(func) + r'\s*\(')
    label = '{0}:{1}'.format(rel, func)
    if body is None:
      details.append(label + ' callback body is missing')
      continue
    compact = re.sub(r'\s+', ' ', body)
    fast = r'if\s*\(\s*!\s*' + re.escape(IPC_ENABLE_HELPER) + r'\s*\(\s*\)\s*\)\s*\{\s*return\s*;\s*\}'
    if not re.search(fast, compact):
      details.append(label + ' lacks a void disable-all return before IPC bookkeeping')
    helper_idx = body.find(IPC_ENABLE_HELPER)
    mutation_points = [p for p in (body.find('ConnList::instance'), body.find('process_close_fd_event'), body.find('switch (event)'), body.find('sshPluginEnabled = true'), body.find('new ')) if p != -1]
    if helper_idx == -1:
      details.append(label + ' does not call ' + IPC_ENABLE_HELPER + '()')
    elif mutation_points and min(mutation_points) < helper_idx:
      details.append(label + ' can reach IPC bookkeeping before checking disable-all')
  if details: result.fail('ipc-disable-all-fast-pass', 'disable-all IPC wrapper fast-pass contract is broken', details)
  else: result.pass_('ipc-disable-all-fast-pass', 'checked IPC wrappers and helper callbacks fast-pass before DMTCP bookkeeping in disable-all mode')

def validate_no_global_constructors(result, reader):
  details = []
  static_re = re.compile(r'^\s*static\s+(?:const\s+)?(?:(?:std|dmtcp)::)?(' + '|'.join(NON_POD_STATIC_TYPES) + r')\b(?P<rest>.*)$')
  for rel in IPC_SOURCES:
    text = strip_comments(reader.read_static(rel, result, 'ipc-no-global-constructors'))
    for lineno, line in enumerate(text.splitlines(), 1):
      m = static_re.match(line)
      if not m:
        continue
      rest = m.group('rest')
      if not any(marker in rest for marker in (';', '=', '[')):
        continue
      decl_prefix = re.split(r'[=;]', rest, 1)[0]
      if '*' in decl_prefix:
        continue
      details.append('{0}:{1}: {2}'.format(rel, lineno, line.strip()))
  if details: result.fail('ipc-no-global-constructors', 'IPC sources contain static non-POD global state that can add libdmtcp constructors', details)
  else: result.pass_('ipc-no-global-constructors', 'IPC consolidation path uses POD/lazy pointer storage instead of static std containers/strings')

def validate_probe_contract(result, reader):
  text = reader.read_text(PROBE_SOURCE, result, 'probe-source-contract')
  required = ('PASS ipc-built-in-preload: launch and exec preload state valid', 'DMTCP_DISABLE_ALL_PLUGINS', 'disable-all', 'libdmtcp.so', 'libdmtcp_ipc.so', 'socketpair', 'poll(', 'select(', 'epoll', 'eventfd', 'mq_open', 'posix_openpt', 'execvp')
  missing = [t for t in required if t not in text]
  if missing: result.fail('probe-source-contract', 'runtime probe source is missing required coverage', missing)
  else: result.pass_('probe-source-contract', 'runtime probe covers launch/exec preload, disable-all, sockets, poll/select, epoll/eventfd, mqueue, and pty')

def run_static(result, reader=None):
  reader = reader or Reader()
  validate_static_scope(result, reader); validate_makefiles(result, reader); validate_no_dso(result, reader)
  validate_helpers(result, reader); validate_helper_header_boundary(result, reader); validate_launch(result, reader); validate_pluginmanager(result, reader)
  validate_no_nested(result, reader); validate_abi(result, reader); validate_ipc_enable_helper(result, reader)
  validate_disable_all_fast_pass(result, reader); validate_no_global_constructors(result, reader)

def pid_files():
  root = REPO_ROOT / 'src/plugin/pid'
  out = []
  for d, dirs, files in os.walk(str(root)):
    dirs[:] = [x for x in dirs if x not in HIDDEN]
    for f in files:
      p = Path(d) / f
      if p.suffix in ('.c', '.cc', '.cpp', '.h', '.hh', '.hpp'): out.append(p)
  return sorted(out)

def run_overlap(result):
  ipc_owner = (REPO_ROOT / 'src/plugin/ipc/file/posixipcwrappers.cpp').read_text(errors='ignore')
  owner_text = strip_comments(ipc_owner)
  owner_details = []
  for token in ('mq_notify', 'SIGEV_THREAD_ID', 'dmtcp_virtual_to_real_pid', 'virtualToRealPidIfEnabled'):
    if token not in owner_text:
      owner_details.append('src/plugin/ipc/file/posixipcwrappers.cpp missing ' + token)
  if owner_details:
    result.fail('overlap-composed-owner', 'POSIX mqueue owner is missing composed PID translation', owner_details)
  else:
    result.pass_('overlap-composed-owner', 'POSIX mqueue mq_notify owns SIGEV_THREAD_ID PID translation')

  unexpected = []
  for p in pid_files():
    rel = str(p.relative_to(REPO_ROOT))
    orig = p.read_text(errors='ignore')
    text = strip_comments(orig)
    for i, line in enumerate(text.splitlines(), 1):
      if re.search(r'\bmq_notify\b', line):
        unexpected.append('{0}:{1}: {2}'.format(rel, i, orig.splitlines()[i - 1].strip()))
  if unexpected: result.fail('overlap-locations', 'IPC/PID mq_notify overlap remains in PID source after composition', unexpected)
  else: result.pass_('overlap-locations', 'no mq_notify IPC/PID overlap remains in PID source files')

def run_artifacts(result):
  lib = REPO_ROOT / 'lib'
  offenders = []
  if not lib.exists(): result.fail('artifacts-lib-output', 'artifacts mode requires generated lib/ outputs', ['lib/'])
  else:
    for p in sorted(lib.rglob('*')):
      rel = str(p.relative_to(REPO_ROOT))
      if 'libdmtcp_ipc' in rel: offenders.append(rel)
      if p.is_symlink():
        try: target = os.readlink(str(p))
        except OSError: target = ''
        if 'libdmtcp_ipc' in target: offenders.append(rel + ' -> ' + target)
    if offenders: result.fail('artifacts-lib-output', 'generated lib outputs contain removed IPC DSO artifact', offenders)
    else: result.pass_('artifacts-lib-output', 'generated lib outputs contain no libdmtcp_ipc.so artifact')
  launch = REPO_ROOT / 'bin/dmtcp_launch'
  if not launch.exists(): result.fail('artifacts-launch-binary', 'artifacts mode requires built bin/dmtcp_launch', ['bin/dmtcp_launch']); return
  data = launch.read_bytes()
  if b'libdmtcp_ipc' in data: result.fail('artifacts-launch-binary', 'built dmtcp_launch still embeds removed IPC DSO name', ['bin/dmtcp_launch contains libdmtcp_ipc'])
  else: result.pass_('artifacts-launch-binary', 'built dmtcp_launch embeds no libdmtcp_ipc.so string')

def run_runtime_probe(result, disable_all=False):
  launch = REPO_ROOT / 'bin/dmtcp_launch'; probe = REPO_ROOT / PROBE
  contract = 'runtime-probe-disable-all' if disable_all else 'runtime-probe-normal'
  missing = [str(x) for x in (launch, probe) if not x.exists()]
  if missing: result.fail(contract, 'runtime probe requires built launch/probe artifacts', missing); return
  cmd = [str(launch), '--coord-port', '0', '--quiet'] + (['--disable-all-plugins'] if disable_all else []) + [str(probe)]
  env = os.environ.copy(); env.pop('DMTCP_IPC_BUILT_IN_PRELOAD_AFTER_EXEC', None)
  try:
    cp = subprocess.run(cmd, cwd=str(REPO_ROOT), env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, universal_newlines=True, timeout=60)
  except subprocess.TimeoutExpired as exc:
    result.fail(contract, 'runtime probe timed out', ['output: ' + str(exc.output)[-2000:]]); return
  expected = 'PASS ipc-built-in-preload: launch and exec preload state valid' + (' (disable-all)' if disable_all else '')
  if cp.returncode != 0 or expected not in (cp.stdout or ''):
    result.fail(contract, 'runtime probe failed', ['exit code: {0}'.format(cp.returncode), 'expected line: ' + expected, 'output: ' + (cp.stdout or '')[-2000:]])
  else: result.pass_(contract, expected)

def fixtures(extra=None):
  f = {rel: '/* fixture: no nested initializer */\n' for rel in STATIC_INPUTS}
  src = '__d_libdir__libdmtcp_so_SOURCES = ' + ' '.join(IPC_MAKE_SOURCES) + '\n'
  obj = 'am___d_libdir__libdmtcp_so_OBJECTS = ' + ' '.join(IPC_OBJECTS) + '\n'
  dep = 'am__depfiles_remade = ' + ' '.join(IPC_DEPFILES) + '\n'
  helper = 'noinst_LIBRARIES += libssh.a\nlibssh_a_SOURCES = ipc/ssh/util_ssh.cpp ipc/ssh/util_ssh.h\nbin_PROGRAMS += $(d_bindir)/dmtcp_ssh $(d_bindir)/dmtcp_sshd\n__d_bindir__dmtcp_ssh_SOURCES = ipc/ssh/dmtcp_ssh.cpp\n__d_bindir__dmtcp_sshd_SOURCES = ipc/ssh/dmtcp_sshd.cpp\n'
  launch = 'static struct PluginInfo pluginInfo[] = {\n { &m, "libdmtcp_modify-env.so" },\n { &u, "libdmtcp_unique-ckpt.so" },\n { &p, "libdmtcp_pathvirt.so" },\n { &l, "libdmtcp.so" }\n};\nstatic void setLDPreloadLibs(bool x) { string preloadLibs=""; if (getenv(ENV_VAR_PLUGIN) != NULL) { preloadLibs += getenv(ENV_VAR_PLUGIN); } if (disableAllPlugins) { preloadLibs = Util::getPath("libdmtcp.so"); } for (size_t i = 0; i < numLibs; i++) {} }\n'
  pm = 'DmtcpPluginDescriptor_t dmtcp_IpcSsh_PluginDescr();\nDmtcpPluginDescriptor_t dmtcp_IpcEvent_PluginDescr();\nDmtcpPluginDescriptor_t dmtcp_IpcFile_PluginDescr();\nDmtcpPluginDescriptor_t dmtcp_IpcPty_PluginDescr();\nDmtcpPluginDescriptor_t dmtcp_IpcSocket_PluginDescr();\nstatic bool ipcPluginEnabled() { const char *disableAllPlugins = getenv(ENV_VAR_DISABLE_ALL_PLUGINS); return disableAllPlugins == NULL || strcmp(disableAllPlugins, "1") != 0; }\nextern "C" void dmtcp_initialize_plugin() { if (ipcPluginEnabled()) { dmtcp_register_plugin(dmtcp_IpcSsh_PluginDescr()); dmtcp_register_plugin(dmtcp_IpcEvent_PluginDescr()); dmtcp_register_plugin(dmtcp_IpcFile_PluginDescr()); dmtcp_register_plugin(dmtcp_IpcPty_PluginDescr()); dmtcp_register_plugin(dmtcp_IpcSocket_PluginDescr()); } void (*fn)() = NEXT_FNC(dmtcp_initialize_plugin); if (fn != NULL) { (*fn)(); } }\n'
  f.update({'src/Makefile.am': src, 'src/Makefile.in': src + obj + dep, 'src/Makefile': src + obj + dep, 'src/plugin/Makefile.am': helper, 'src/plugin/Makefile.in': helper, 'src/plugin/Makefile': helper, 'src/dmtcp_launch.cpp': launch, 'src/execwrappers.cpp': '', 'src/util_exec.cpp': '', 'src/pluginmanager.cpp': pm})
  f['src/plugin/ipc/ipc.h'] = 'static inline bool dmtcp_ipc_wrappers_enabled() { const char *disableAllPlugins = getenv(ENV_VAR_DISABLE_ALL_PLUGINS); return disableAllPlugins == NULL || strcmp(disableAllPlugins, "1") != 0; }\n'
  for rel, func, real in IPC_WRAPPER_FAST_PASS:
    f[rel] = f.get(rel, '') + 'extern "C" int {0}() {{ if (!dmtcp_ipc_wrappers_enabled()) {{ return {1}(); }} DMTCP_PLUGIN_DISABLE_CKPT(); }}\n'.format(func, real)
  for rel, func in IPC_VOID_FAST_PASS:
    f[rel] = f.get(rel, '') + 'void {0}() {{ if (!dmtcp_ipc_wrappers_enabled()) {{ return; }} switch (event) {{}} }}\n'.format(func)
  if extra: f.update(extra)
  return f

def run_self_test(result):
  r = Result(False); validate_static_scope(r, Reader(fixtures({'.gsd/secret.md': 'x'})), STATIC_INPUTS + ('.gsd/secret.md',))
  result.pass_('self-test-hidden-path', 'hidden .gsd input fixture is rejected') if 'static-input-scope' in contracts(r) else result.fail('self-test-hidden-path', 'hidden .gsd input fixture was not rejected')
  r = Result(False); validate_static_scope(r, Reader(fixtures({'test/unapproved.c': 'int x;'})), STATIC_INPUTS + ('test/unapproved.c',))
  result.pass_('self-test-source-scope', 'non-src input fixture is rejected') if 'static-input-scope' in contracts(r) else result.fail('self-test-source-scope', 'non-src input fixture was not rejected')
  r = Result(False); validate_launch(r, Reader(fixtures()))
  result.pass_('self-test-optional-launch-rows', 'optional modify-env/unique-ckpt/pathvirt rows are accepted before libdmtcp.so') if not r.failures else result.fail('self-test-optional-launch-rows', 'optional final-state launcher rows were rejected', ['{0}: {1}'.format(c,m) for c,m,_ in r.failures])
  stale_alloc_dl_launch = 'static struct PluginInfo pluginInfo[] = {\n { &a, "libdmtcp_alloc.so" },\n { &d, "libdmtcp_dl.so" },\n { &l, "libdmtcp.so" }\n};\nstatic void setLDPreloadLibs(bool x) { string preloadLibs=""; if (getenv(ENV_VAR_PLUGIN) != NULL) { preloadLibs += getenv(ENV_VAR_PLUGIN); } if (disableAllPlugins) { preloadLibs = Util::getPath("libdmtcp.so"); } for (size_t i = 0; i < numLibs; i++) {} }\n'
  r = Result(False); validate_launch(r, Reader(fixtures({'src/dmtcp_launch.cpp': stale_alloc_dl_launch})))
  result.pass_('self-test-stale-alloc-dl-launch-rows', 'stale alloc/DL launcher rows are rejected with final-state diagnostics') if 'launch-preload-state' in contracts(r) else result.fail('self-test-stale-alloc-dl-launch-rows', 'stale alloc/DL launcher rows were not rejected')
  missing_libdmtcp_launch = 'static struct PluginInfo pluginInfo[] = {\n { &m, "libdmtcp_modify-env.so" },\n { &p, "libdmtcp_pathvirt.so" }\n};\nstatic void setLDPreloadLibs(bool x) { string preloadLibs=""; if (getenv(ENV_VAR_PLUGIN) != NULL) { preloadLibs += getenv(ENV_VAR_PLUGIN); } if (disableAllPlugins) { preloadLibs = Util::getPath("libdmtcp.so"); } for (size_t i = 0; i < numLibs; i++) {} }\n'
  r = Result(False); validate_launch(r, Reader(fixtures({'src/dmtcp_launch.cpp': missing_libdmtcp_launch})))
  result.pass_('self-test-missing-libdmtcp-launch-row', 'missing libdmtcp.so launcher row is rejected') if 'launch-preload-state' in contracts(r) else result.fail('self-test-missing-libdmtcp-launch-row', 'missing libdmtcp.so launcher row was not rejected')
  r = Result(False); rd = Reader(fixtures({'src/plugin/Makefile.am': fixtures()['src/plugin/Makefile.am'] + 'libdmtcp_PROGRAMS += $(d_libdir)/libdmtcp_ipc.so\n', 'src/dmtcp_launch.cpp': 'static struct PluginInfo pluginInfo[] = { { &x, "libdmtcp_ipc.so" } };\nstatic void setLDPreloadLibs(bool x) { string preloadLibs = Util::getPath("libdmtcp_ipc.so"); }\n'})); validate_no_dso(r, rd); validate_launch(r, rd)
  result.pass_('self-test-stale-dso', 'stale IPC DSO target and launch row fixture is rejected') if {'no-libdmtcp-ipc-dso','launch-preload-state'}.issubset(contracts(r)) else result.fail('self-test-stale-dso', 'stale IPC DSO fixture was not rejected', [str(contracts(r))])
  wrong = fixtures({'src/pluginmanager.cpp': fixtures()['src/pluginmanager.cpp'].replace('dmtcp_register_plugin(dmtcp_IpcEvent_PluginDescr()); dmtcp_register_plugin(dmtcp_IpcFile_PluginDescr()); dmtcp_register_plugin(dmtcp_IpcPty_PluginDescr()); dmtcp_register_plugin(dmtcp_IpcSocket_PluginDescr());', 'dmtcp_register_plugin(dmtcp_IpcSocket_PluginDescr()); dmtcp_register_plugin(dmtcp_IpcEvent_PluginDescr()); dmtcp_register_plugin(dmtcp_IpcFile_PluginDescr()); dmtcp_register_plugin(dmtcp_IpcPty_PluginDescr());')})
  r = Result(False); validate_pluginmanager(r, Reader(wrong))
  result.pass_('self-test-descriptor-order', 'wrong descriptor order fixture is rejected') if 'pluginmanager-ipc-descriptors' in contracts(r) else result.fail('self-test-descriptor-order', 'wrong descriptor order fixture was not rejected')
  duplicate = fixtures({'src/pluginmanager.cpp': fixtures()['src/pluginmanager.cpp'].replace('dmtcp_register_plugin(dmtcp_IpcEvent_PluginDescr()); dmtcp_register_plugin(dmtcp_IpcFile_PluginDescr());', 'dmtcp_register_plugin(dmtcp_IpcEvent_PluginDescr()); dmtcp_register_plugin(dmtcp_IpcEvent_PluginDescr()); dmtcp_register_plugin(dmtcp_IpcFile_PluginDescr());')})
  r = Result(False); validate_pluginmanager(r, Reader(duplicate))
  result.pass_('self-test-duplicate-descriptor', 'duplicate IPC descriptor registration fixture is rejected') if 'pluginmanager-ipc-descriptors' in contracts(r) else result.fail('self-test-duplicate-descriptor', 'duplicate IPC descriptor registration fixture was not rejected')
  r = Result(False); validate_helpers(r, Reader(fixtures({'src/plugin/Makefile.am': '', 'src/plugin/Makefile.in': '', 'src/plugin/Makefile': ''})))
  result.pass_('self-test-helper-preservation', 'deleted SSH helper fixture is rejected') if 'ipc-helper-preservation' in contracts(r) else result.fail('self-test-helper-preservation', 'deleted SSH helper fixture was not rejected')
  r = Result(False); validate_helper_header_boundary(r, Reader(fixtures({'src/plugin/ipc/ipc.h': '#include "constants.h"\n' + fixtures()['src/plugin/ipc/ipc.h']})))
  result.pass_('self-test-helper-header-boundary', 'src/constants.h dependency in ipc.h fixture is rejected') if 'ipc-helper-header-boundary' in contracts(r) else result.fail('self-test-helper-header-boundary', 'src/constants.h dependency fixture was not rejected')
  r = Result(False); validate_no_nested(r, Reader(fixtures({'src/plugin/ipc/ipc.cpp': 'EXTERNC void dmtcp_initialize_plugin() { NEXT_FNC(dmtcp_initialize_plugin)(); }\n'})))
  result.pass_('self-test-nested-initializer', 'nested IPC initializer fixture is rejected') if 'ipc-no-nested-initializer' in contracts(r) else result.fail('self-test-nested-initializer', 'nested IPC initializer fixture was not rejected')
  r = Result(False); validate_probe_contract(r, Reader({PROBE_SOURCE: 'int main(void){return 0;}\n'}))
  result.pass_('self-test-disable-all-probe', 'probe missing disable-all coverage fixture is rejected') if 'probe-source-contract' in contracts(r) else result.fail('self-test-disable-all-probe', 'probe missing disable-all fixture was not rejected')
  r = Result(False); validate_disable_all_fast_pass(r, Reader(fixtures({'src/plugin/ipc/socket/socketwrappers.cpp': 'extern "C" int socket() { DMTCP_PLUGIN_DISABLE_CKPT(); return _real_socket(); }\n'})))
  result.pass_('self-test-disable-all-fast-pass', 'wrapper missing disable-all fast-pass fixture is rejected') if 'ipc-disable-all-fast-pass' in contracts(r) else result.fail('self-test-disable-all-fast-pass', 'missing disable-all fast-pass fixture was not rejected')
  r = Result(False); validate_no_global_constructors(r, Reader(fixtures({'src/plugin/ipc/ssh/ssh.cpp': 'static string cmd;\n'})))
  result.pass_('self-test-global-constructor', 'static std string fixture is rejected') if 'ipc-no-global-constructors' in contracts(r) else result.fail('self-test-global-constructor', 'static std string fixture was not rejected')
  r = Result(False); run_static(r, Reader(fixtures()))
  result.pass_('self-test-positive-fixture', 'positive static fixture satisfies IPC contracts') if not r.failures else result.fail('self-test-positive-fixture', 'positive fixture unexpectedly failed', ['{0}: {1}'.format(c,m) for c,m,_ in r.failures])
  r = Result(False); validate_probe_contract(r, Reader())
  result.pass_('self-test-current-probe-source', 'current runtime probe contains required coverage') if not r.failures else result.fail('self-test-current-probe-source', 'current runtime probe is missing required coverage', ['{0}: {1}'.format(c,m) for c,m,_ in r.failures])

def main(argv):
  ap = argparse.ArgumentParser(description=__doc__)
  ap.add_argument('mode', nargs='?', default='static', choices=('static','artifacts','overlap','self-test','full'))
  args = ap.parse_args(argv)
  result = Result()
  if args.mode == 'static': run_static(result)
  elif args.mode == 'artifacts': run_artifacts(result)
  elif args.mode == 'overlap': run_overlap(result)
  elif args.mode == 'self-test': run_self_test(result)
  elif args.mode == 'full': run_static(result); run_self_test(result); validate_probe_contract(result, Reader()); run_overlap(result); run_artifacts(result); run_runtime_probe(result, False); run_runtime_probe(result, True)
  return result.finish()

if __name__ == '__main__':
  sys.exit(main(sys.argv[1:]))
