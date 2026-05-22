#!/usr/bin/env python3
"""Executable gate for moving the alloc wrapper group into libdmtcp.so.

Modes:
  static     source/config-only checks for the final alloc built-in state
  wrappers   dmtcp_alloc_enabled() predicate and wrapper fast-pass checks
  artifacts  built-output stale libdmtcp_alloc.so checks
  runtime    run launch/exec preload probes in normal and disabled modes
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

ALLOC_SOURCES = (
  'src/plugin/alloc/alloc.h',
  'src/plugin/alloc/mallocwrappers.cpp',
  'src/plugin/alloc/mmapwrappers.cpp',
)
ALLOC_MAKE_SOURCES = tuple(p.replace('src/', '', 1) for p in ALLOC_SOURCES)
ALLOC_CPP_SOURCES = tuple(p for p in ALLOC_MAKE_SOURCES if p.endswith('.cpp'))
ALLOC_OBJECTS = tuple(p.replace('.cpp', '.$(OBJEXT)') for p in ALLOC_CPP_SOURCES)
ALLOC_DEPFILES = tuple(
  p.rsplit('/', 1)[0] + '/$(DEPDIR)/' + p.rsplit('/', 1)[1].replace('.cpp', '.Po')
  for p in ALLOC_CPP_SOURCES
)

STATIC_INPUTS = (
  'include/dmtcp.h',
  'src/Makefile.am', 'src/Makefile.in', 'src/Makefile',
  'src/plugin/Makefile.am', 'src/plugin/Makefile.in', 'src/plugin/Makefile',
  'src/dmtcp_launch.cpp', 'src/pluginmanager.cpp',
) + ALLOC_SOURCES

TEST_INPUTS = ('test/Makefile.in', 'test/Makefile', 'test/alloc-built-in-preload.c')
FORBIDDEN_DSO = ('libdmtcp_alloc.so', 'libdmtcp_alloc')
PROBE = 'test/alloc-built-in-preload'
PROBE_SOURCE = 'test/alloc-built-in-preload.c'
AFTER_EXEC_ENV = 'DMTCP_ALLOC_BUILT_IN_PRELOAD_AFTER_EXEC'

ALLOC_ACCESSOR_PATTERNS = (
  'dmtcp_Alloc_PluginDescr',
  'dmtcp_AllocPluginDescr',
  'Alloc_PluginDescr',
  'AllocPluginDescr',
  'allocPluginDescr',
)
ALLOC_DESCRIPTOR_PATTERNS = ALLOC_ACCESSOR_PATTERNS + (
  'DMTCP_DECL_PLUGIN',
  'dmtcp_initialize_plugin',
  'DmtcpPluginDescriptor_t',
)

MALLOC_WRAPPERS = (
  ('calloc', '_real_calloc'),
  ('malloc', '_real_malloc'),
  ('memalign', '_real_memalign'),
  ('posix_memalign', '_real_posix_memalign'),
  ('valloc', '_real_valloc'),
  ('free', '_real_free'),
  ('realloc', '_real_realloc'),
)
MMAP_WRAPPERS = (
  ('mmap', '_real_mmap'),
  ('mmap64', '_real_mmap64'),
  ('munmap', '_real_munmap'),
  ('mremap', '_real_mremap'),
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

def function_body(text, symbol):
  clean = strip_comments(text)
  pattern = re.compile(r'(?:^|\n)\s*(?:extern\s+"C"\s*)?(?:EXTERNC\s+)?'
                       r'(?:(?:[A-Za-z_][\w:<>,~]*\s+)|(?:[\*&]\s*))*' +
                       re.escape(symbol) + r'\s*\(', re.M)
  for match in pattern.finditer(clean):
    brace = clean.find('{', match.end())
    semi = clean.find(';', match.end())
    if brace != -1 and (semi == -1 or brace < semi):
      body = braced_body(clean, brace)
      if body is not None:
        return body
  return None

def require_items(result, reader, rel, var, items, contract, label):
  block = make_var(reader.read_static(rel, result, contract), var)
  if not block:
    result.fail(contract, 'Makefile variable is missing', ['{0}: {1}'.format(rel, var)])
    return
  missing = [x for x in items if x not in block]
  if missing:
    result.fail(contract, 'Makefile variable is missing alloc built-in entries',
                ['{0}: missing {1}'.format(rel, x) for x in missing])
    return
  result.pass_(contract, '{0}:{1} contains {2} alloc {3}'.format(rel, var, len(items), label))

def require_absent(result, text, rel, needles, contract, msg):
  hits = []
  for needle in needles:
    for lineno, line in enumerate(text.splitlines(), 1):
      if needle in line:
        hits.append('{0}:{1}: {2}'.format(rel, lineno, line.strip()))
  if hits:
    result.fail(contract, msg, hits)
  else:
    result.pass_(contract, '{0} omits stale alloc DSO tokens'.format(rel))

def validate_static_scope(result, reader, inputs=STATIC_INPUTS):
  details = []
  seen = set()
  for rel in inputs:
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
    if 'alloc-built-in-preload: alloc-built-in-preload.c' not in text:
      result.fail('probe-make-target', 'alloc preload probe target is missing', [rel])
    elif '$(CC) -o $@ $< $(CFLAGS)' not in text and '${CC} -o $@ $< $(CFLAGS)' not in text:
      result.fail('probe-make-target', 'alloc preload probe target does not compile with CFLAGS', [rel])
    else:
      result.pass_('probe-make-target', '{0} builds alloc-built-in-preload'.format(rel))
  source = reader.read_test(PROBE_SOURCE, result, 'probe-source-contract')
  required = (
    'libdmtcp_alloc.so', 'libdmtcp.so', 'DMTCP_HIJACK_LIBS',
    'DMTCP_HIJACK_LIBS_M32', 'DMTCP_ALLOC_PLUGIN',
    'DMTCP_DISABLE_ALL_PLUGINS', AFTER_EXEC_ENV,
    'malloc(', 'calloc(', 'realloc(', 'memalign(', 'posix_memalign(',
    'valloc(', 'mmap(', '/proc/self/maps', 'execvp'
  )
  missing = [token for token in required if token not in source]
  if missing:
    result.fail('probe-source-contract', 'probe source is missing required diagnostic/runtime token', missing)
  else:
    result.pass_('probe-source-contract', 'probe source checks preload state, alloc wrappers, mmap, maps, and exec reconstruction')

def validate_makefile_ownership(result, reader):
  for rel in ('src/Makefile.am', 'src/Makefile.in', 'src/Makefile'):
    require_items(result, reader, rel, '__d_libdir__libdmtcp_so_SOURCES',
                  ALLOC_MAKE_SOURCES, 'makefile-libdmtcp-alloc-sources', 'sources')
  for rel in ('src/Makefile.in', 'src/Makefile'):
    require_items(result, reader, rel, 'am___d_libdir__libdmtcp_so_OBJECTS',
                  ALLOC_OBJECTS, 'makefile-libdmtcp-alloc-objects', 'objects')
    require_items(result, reader, rel, 'am__depfiles_remade',
                  ALLOC_DEPFILES, 'makefile-libdmtcp-alloc-dependencies', 'depfiles')
  for rel in ('src/plugin/Makefile.am', 'src/plugin/Makefile.in',
              'src/plugin/Makefile', 'src/dmtcp_launch.cpp'):
    require_absent(result, reader.read_static(rel, result, 'stale-alloc-dso-source'),
                   rel, FORBIDDEN_DSO, 'stale-alloc-dso-source',
                   'stale alloc DSO launcher/build token is present')

def validate_descriptor_absence(result, reader):
  details = []
  pm = strip_comments(reader.read_static('src/pluginmanager.cpp', result, 'alloc-descriptor-absence'))
  public_header = strip_comments(reader.read_static('include/dmtcp.h', result, 'alloc-accessor-private'))
  for token in ALLOC_ACCESSOR_PATTERNS:
    if token in pm:
      details.append('src/pluginmanager.cpp: unexpected alloc descriptor/accessor token {0}'.format(token))
    if token in public_header:
      details.append('include/dmtcp.h: unexpected alloc descriptor/accessor token {0}'.format(token))
  if 'dmtcp_alloc_enabled' in public_header:
    details.append('include/dmtcp.h: dmtcp_alloc_enabled leaked into public plugin ABI')
  for rel in ALLOC_SOURCES:
    text = strip_comments(reader.read_static(rel, result, 'alloc-descriptor-absence'))
    for token in ALLOC_DESCRIPTOR_PATTERNS:
      if token in text:
        details.append('{0}: unexpected plugin descriptor/public initializer token {1}'.format(rel, token))
  if details:
    result.fail('alloc-descriptor-absence', 'alloc wrapper group acquired plugin descriptor/API surface', details)
  else:
    result.pass_('alloc-descriptor-absence', 'alloc remains a wrapper-only built-in group with no descriptor or public accessor')

def validate_alloc_predicate(result, reader):
  alloc_h = strip_comments(reader.read_static('src/plugin/alloc/alloc.h', result, 'alloc-enable-predicate'))
  malloc_src = strip_comments(reader.read_static('src/plugin/alloc/mallocwrappers.cpp', result, 'alloc-enable-predicate'))
  details = []
  if 'dmtcp_alloc_enabled' not in alloc_h:
    details.append('src/plugin/alloc/alloc.h: missing dmtcp_alloc_enabled declaration')
  body = function_body(malloc_src, 'dmtcp_alloc_enabled')
  if body is None:
    details.append('src/plugin/alloc/mallocwrappers.cpp: missing dmtcp_alloc_enabled() definition')
  else:
    env_markers = (
      ('DMTCP_ALLOC_PLUGIN', 'ENV_VAR_ALLOC_PLUGIN'),
      ('DMTCP_DISABLE_ALL_PLUGINS', 'ENV_VAR_DISABLE_ALL_PLUGINS'),
    )
    for literal, macro in env_markers:
      has_literal = literal in body
      has_local_fallback = macro in body and macro in alloc_h and literal in alloc_h
      if not (has_literal or has_local_fallback):
        details.append('src/plugin/alloc/mallocwrappers.cpp: dmtcp_alloc_enabled() missing {0} marker or {1} local fallback'.format(literal, macro))
    if re.search(r'return\s+1\s*;', body) and not re.search(r'return\s+0\s*;', body):
      details.append('src/plugin/alloc/mallocwrappers.cpp: dmtcp_alloc_enabled() appears to be unconditional')
  if details:
    result.fail('alloc-enable-predicate', 'alloc enable predicate markers are missing or unconditional', details)
  else:
    result.pass_('alloc-enable-predicate', 'dmtcp_alloc_enabled() is declared and keyed by alloc/disable-all controls')

def validate_wrapper_fast_pass(result, reader, rel, wrappers, contract):
  text = reader.read_static(rel, result, contract)
  details = []
  for symbol, real_symbol in wrappers:
    body = function_body(text, symbol)
    if body is None:
      details.append('{0}: missing wrapper definition for {1}'.format(rel, symbol))
      continue
    pred = body.find('dmtcp_alloc_enabled')
    disable = body.find('DMTCP_PLUGIN_DISABLE_CKPT')
    real_call = body.find(real_symbol)
    if pred == -1:
      details.append('{0}:{1}: missing dmtcp_alloc_enabled() disabled fast-pass'.format(rel, symbol))
    if real_call == -1:
      details.append('{0}:{1}: missing {2} call'.format(rel, symbol, real_symbol))
    if disable == -1:
      details.append('{0}:{1}: missing DMTCP_PLUGIN_DISABLE_CKPT guard'.format(rel, symbol))
    if pred != -1 and disable != -1 and pred > disable:
      details.append('{0}:{1}: dmtcp_alloc_enabled() check must occur before checkpoint disable'.format(rel, symbol))
    if pred != -1 and real_call != -1 and disable != -1 and not (pred < real_call < disable):
      details.append('{0}:{1}: fast-pass real call should occur before checkpoint disable'.format(rel, symbol))
  if details:
    result.fail(contract, 'wrapper disabled fast-pass markers are missing or misplaced', details)
  else:
    result.pass_(contract, '{0} wrappers fast-pass to real functions when alloc is disabled'.format(rel))

def validate_mmap_boundary(result, reader):
  rel = 'src/plugin/alloc/mmapwrappers.cpp'
  text = reader.read_static(rel, result, 'mmap-compile-boundary')
  clean = strip_comments(text)
  details = []
  start = clean.find('#ifdef ENABLE_MMAP_WRAPPERS')
  end = clean.rfind('#endif')
  if start == -1:
    details.append('{0}: missing #ifdef ENABLE_MMAP_WRAPPERS'.format(rel))
  if end == -1 or (start != -1 and end < start):
    details.append('{0}: missing #endif for ENABLE_MMAP_WRAPPERS boundary'.format(rel))
  if start != -1 and end != -1 and end > start:
    for symbol, _real_symbol in MMAP_WRAPPERS:
      match = re.search(r'(?:^|\n)\s*(?:extern\s+"C"\s*)?'
                        r'(?:(?:[A-Za-z_][\w:<>,~]*\s+)|(?:[\*&]\s*))*' +
                        re.escape(symbol) + r'\s*\(', clean, re.M)
      if match and not (start < match.start() < end):
        details.append('{0}:{1}: wrapper definition is outside ENABLE_MMAP_WRAPPERS'.format(rel, symbol))
  if details:
    result.fail('mmap-compile-boundary', 'ENABLE_MMAP_WRAPPERS no longer encloses mmap-family wrappers', details)
  else:
    result.pass_('mmap-compile-boundary', 'ENABLE_MMAP_WRAPPERS remains the compile-time boundary for mmap-family wrappers')

def validate_wrappers(result, reader):
  validate_alloc_predicate(result, reader)
  validate_wrapper_fast_pass(result, reader, 'src/plugin/alloc/mallocwrappers.cpp',
                             MALLOC_WRAPPERS, 'malloc-wrapper-fast-pass')
  validate_mmap_boundary(result, reader)
  validate_wrapper_fast_pass(result, reader, 'src/plugin/alloc/mmapwrappers.cpp',
                             MMAP_WRAPPERS, 'mmap-wrapper-fast-pass')

def validate_static(result, reader):
  validate_static_scope(result, reader)
  validate_makefile_ownership(result, reader)
  validate_descriptor_absence(result, reader)
  validate_wrappers(result, reader)
  validate_test_wiring(result, reader)

def validate_artifacts(result):
  lib_dir = REPO_ROOT / 'lib'
  found = []
  if lib_dir.exists():
    for path in lib_dir.rglob('*'):
      if 'libdmtcp_alloc' in path.name:
        found.append(str(path.relative_to(REPO_ROOT)))
  if found:
    result.fail('stale-alloc-dso-artifact', 'built alloc DSO artifact is still present', found)
  else:
    result.pass_('stale-alloc-dso-artifact', 'no libdmtcp_alloc artifact found under lib/')
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
  if 'libdmtcp_alloc' in proc.stdout:
    result.fail('launcher-string-artifact', 'dmtcp_launch still embeds libdmtcp_alloc', ['strings bin/dmtcp_launch contains libdmtcp_alloc'])
  else:
    result.pass_('launcher-string-artifact', 'dmtcp_launch strings omit libdmtcp_alloc')

def run_probe(result, args, contract, env=None):
  proc = subprocess.run(args, cwd=str(REPO_ROOT), text=True, env=env,
                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
  if proc.returncode != 0:
    details = ['command: {0}'.format(' '.join(args)), 'exit: {0}'.format(proc.returncode)]
    if proc.stdout.strip():
      details.append('stdout: {0}'.format(proc.stdout.strip()[-1000:]))
    if proc.stderr.strip():
      details.append('stderr: {0}'.format(proc.stderr.strip()[-1000:]))
    result.fail(contract, 'alloc preload runtime probe failed', details)
  else:
    msg = proc.stdout.strip().splitlines()[-1] if proc.stdout.strip() else 'exit 0'
    result.pass_(contract, msg)

def validate_runtime(result):
  probe = REPO_ROOT / PROBE
  launcher = REPO_ROOT / 'bin' / 'dmtcp_launch'
  if not probe.exists():
    result.fail('runtime-probe-built', 'alloc preload probe binary is missing', [PROBE])
    return
  if not os.access(str(probe), os.X_OK):
    result.fail('runtime-probe-built', 'alloc preload probe is not executable', [PROBE])
    return
  if not launcher.exists():
    result.fail('runtime-probe-launcher', 'dmtcp_launch binary is missing', ['bin/dmtcp_launch'])
    return
  result.pass_('runtime-probe-built', 'alloc preload probe and dmtcp_launch exist')
  run_probe(result, ['bin/dmtcp_launch', '--coord-port', '0', '--quiet', './' + PROBE], 'runtime-probe-normal')
  run_probe(result, ['bin/dmtcp_launch', '--coord-port', '0', '--quiet', '--disable-alloc-plugin', './' + PROBE],
            'runtime-probe-disable-alloc-flag')
  env = os.environ.copy()
  env['DMTCP_ALLOC_PLUGIN'] = '0'
  run_probe(result, ['bin/dmtcp_launch', '--coord-port', '0', '--quiet', './' + PROBE],
            'runtime-probe-disable-alloc-env', env=env)
  run_probe(result, ['bin/dmtcp_launch', '--coord-port', '0', '--quiet', '--disable-all-plugins', './' + PROBE],
            'runtime-probe-disable-all')

def validate_test_wiring(result, reader):
  validate_probe_target(result, reader)

def positive_fixtures():
  malloc_source = r'''
#include "alloc.h"
EXTERNC int
dmtcp_alloc_enabled()
{
  const char *alloc = getenv("DMTCP_ALLOC_PLUGIN");
  const char *all = getenv("DMTCP_DISABLE_ALL_PLUGINS");
  if ((alloc != NULL && strcmp(alloc, "0") == 0) ||
      (all != NULL && strcmp(all, "1") == 0)) return 0;
  return 1;
}
extern "C" void *calloc(size_t nmemb, size_t size) { if (!dmtcp_alloc_enabled()) return _real_calloc(nmemb, size); DMTCP_PLUGIN_DISABLE_CKPT(); void *r = _real_calloc(nmemb, size); DMTCP_PLUGIN_ENABLE_CKPT(); return r; }
extern "C" void *malloc(size_t size) { if (!dmtcp_alloc_enabled()) return _real_malloc(size); DMTCP_PLUGIN_DISABLE_CKPT(); void *r = _real_malloc(size); DMTCP_PLUGIN_ENABLE_CKPT(); return r; }
extern "C" void *memalign(size_t boundary, size_t size) { if (!dmtcp_alloc_enabled()) return _real_memalign(boundary, size); DMTCP_PLUGIN_DISABLE_CKPT(); void *r = _real_memalign(boundary, size); DMTCP_PLUGIN_ENABLE_CKPT(); return r; }
extern "C" int posix_memalign(void **memptr, size_t alignment, size_t size) { if (!dmtcp_alloc_enabled()) return _real_posix_memalign(memptr, alignment, size); DMTCP_PLUGIN_DISABLE_CKPT(); int r = _real_posix_memalign(memptr, alignment, size); DMTCP_PLUGIN_ENABLE_CKPT(); return r; }
extern "C" void *valloc(size_t size) { if (!dmtcp_alloc_enabled()) return _real_valloc(size); DMTCP_PLUGIN_DISABLE_CKPT(); void *r = _real_valloc(size); DMTCP_PLUGIN_ENABLE_CKPT(); return r; }
extern "C" void free(void *ptr) { if (!dmtcp_alloc_enabled()) { _real_free(ptr); return; } DMTCP_PLUGIN_DISABLE_CKPT(); _real_free(ptr); DMTCP_PLUGIN_ENABLE_CKPT(); }
extern "C" void *realloc(void *ptr, size_t size) { if (!dmtcp_alloc_enabled()) return _real_realloc(ptr, size); DMTCP_PLUGIN_DISABLE_CKPT(); void *r = _real_realloc(ptr, size); DMTCP_PLUGIN_ENABLE_CKPT(); return r; }
'''
  mmap_source = r'''
#include "alloc.h"
#define ENABLE_MMAP_WRAPPERS 1
#ifdef ENABLE_MMAP_WRAPPERS
extern "C" void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) { if (!dmtcp_alloc_enabled()) return _real_mmap(addr, length, prot, flags, fd, offset); DMTCP_PLUGIN_DISABLE_CKPT(); void *r = _real_mmap(addr, length, prot, flags, fd, offset); DMTCP_PLUGIN_ENABLE_CKPT(); return r; }
extern "C" void *mmap64(void *addr, size_t length, int prot, int flags, int fd, off64_t offset) { if (!dmtcp_alloc_enabled()) return _real_mmap64(addr, length, prot, flags, fd, offset); DMTCP_PLUGIN_DISABLE_CKPT(); void *r = _real_mmap64(addr, length, prot, flags, fd, offset); DMTCP_PLUGIN_ENABLE_CKPT(); return r; }
extern "C" int munmap(void *addr, size_t length) { if (!dmtcp_alloc_enabled()) return _real_munmap(addr, length); DMTCP_PLUGIN_DISABLE_CKPT(); int r = _real_munmap(addr, length); DMTCP_PLUGIN_ENABLE_CKPT(); return r; }
extern "C" void *mremap(void *old_address, size_t old_size, size_t new_size, int flags, ...) { if (!dmtcp_alloc_enabled()) return _real_mremap(old_address, old_size, new_size, flags); DMTCP_PLUGIN_DISABLE_CKPT(); void *r = _real_mremap(old_address, old_size, new_size, flags); DMTCP_PLUGIN_ENABLE_CKPT(); return r; }
#endif // ENABLE_MMAP_WRAPPERS
'''
  return {
    'include/dmtcp.h': '/* public plugin ABI intentionally omits alloc built-in accessors */\n',
    'src/Makefile.am': '__d_libdir__libdmtcp_so_SOURCES = ' + ' '.join(ALLOC_MAKE_SOURCES) + '\n',
    'src/Makefile.in': '__d_libdir__libdmtcp_so_SOURCES = ' + ' '.join(ALLOC_MAKE_SOURCES) + '\n' +
                       'am___d_libdir__libdmtcp_so_OBJECTS = ' + ' '.join(ALLOC_OBJECTS) + '\n' +
                       'am__depfiles_remade = ' + ' '.join(ALLOC_DEPFILES) + '\n',
    'src/Makefile': '__d_libdir__libdmtcp_so_SOURCES = ' + ' '.join(ALLOC_MAKE_SOURCES) + '\n' +
                    'am___d_libdir__libdmtcp_so_OBJECTS = ' + ' '.join(ALLOC_OBJECTS) + '\n' +
                    'am__depfiles_remade = ' + ' '.join(ALLOC_DEPFILES) + '\n',
    'src/plugin/Makefile.am': 'no stale alloc dso target here\n',
    'src/plugin/Makefile.in': 'no stale alloc dso target here\n',
    'src/plugin/Makefile': 'no stale alloc dso target here\n',
    'src/dmtcp_launch.cpp': 'static const char *libs = "libdmtcp.so";\n',
    'src/pluginmanager.cpp': 'dmtcp_register_plugin(dmtcp_Core_PluginDescr());\n',
    'src/plugin/alloc/alloc.h': '#include "dmtcp.h"\nEXTERNC int dmtcp_alloc_enabled();\n#define _real_malloc NEXT_FNC(malloc)\n',
    'src/plugin/alloc/mallocwrappers.cpp': malloc_source,
    'src/plugin/alloc/mmapwrappers.cpp': mmap_source,
    'test/Makefile.in': 'alloc-built-in-preload: alloc-built-in-preload.c\n\t$(CC) -o $@ $< $(CFLAGS)\n',
    'test/Makefile': 'alloc-built-in-preload: alloc-built-in-preload.c\n\t$(CC) -o $@ $< $(CFLAGS)\n',
    PROBE_SOURCE: 'libdmtcp_alloc.so libdmtcp.so DMTCP_HIJACK_LIBS DMTCP_HIJACK_LIBS_M32 DMTCP_ALLOC_PLUGIN DMTCP_DISABLE_ALL_PLUGINS ' + AFTER_EXEC_ENV + ' malloc( calloc( realloc( memalign( posix_memalign( valloc( mmap( /proc/self/maps execvp\n',
  }

def expect_failure(name, validator, contract):
  bad = Result(emit=False)
  validator(bad)
  if not any(c == contract for c, _msg, _details in bad.failures):
    print('FAIL self-test: {0} fixture was not rejected for {1}'.format(name, contract))
    if bad.failures:
      for c, msg, details in bad.failures:
        print('  - observed {0}: {1}: {2}'.format(c, msg, '; '.join(details)))
    return False
  return True

def self_test():
  fixtures = positive_fixtures()
  ok = Result(emit=False)
  reader = Reader(fixtures)
  validate_static(ok, reader)
  if ok.failures:
    print('FAIL self-test: positive fixture unexpectedly failed')
    for contract, msg, details in ok.failures:
      print('  - {0}: {1}: {2}'.format(contract, msg, '; '.join(details)))
    return 1

  macro_fixtures = dict(fixtures)
  macro_fixtures['src/plugin/alloc/alloc.h'] = (
    '#include "dmtcp.h"\n'
    '#ifndef ENV_VAR_ALLOC_PLUGIN\n'
    '# define ENV_VAR_ALLOC_PLUGIN "DMTCP_ALLOC_PLUGIN"\n'
    '#endif\n'
    '#ifndef ENV_VAR_DISABLE_ALL_PLUGINS\n'
    '# define ENV_VAR_DISABLE_ALL_PLUGINS "DMTCP_DISABLE_ALL_PLUGINS"\n'
    '#endif\n'
    'EXTERNC int dmtcp_alloc_enabled();\n'
    '#define _real_malloc NEXT_FNC(malloc)\n'
  )
  macro_fixtures['src/plugin/alloc/mallocwrappers.cpp'] = (
    macro_fixtures['src/plugin/alloc/mallocwrappers.cpp']
    .replace('getenv("DMTCP_ALLOC_PLUGIN")', 'getenv(ENV_VAR_ALLOC_PLUGIN)')
    .replace('getenv("DMTCP_DISABLE_ALL_PLUGINS")', 'getenv(ENV_VAR_DISABLE_ALL_PLUGINS)')
  )
  macro_ok = Result(emit=False)
  validate_wrappers(macro_ok, Reader(macro_fixtures))
  if macro_ok.failures:
    print('FAIL self-test: local ENV_VAR fallback fixture unexpectedly failed')
    for contract, msg, details in macro_ok.failures:
      print('  - {0}: {1}: {2}'.format(contract, msg, '; '.join(details)))
    return 1

  bad_fixtures = dict(fixtures)
  bad_fixtures['src/dmtcp_launch.cpp'] = '"libdmtcp_alloc.so"\n'
  if not expect_failure('stale launcher DSO',
                        lambda r: validate_static(r, Reader(bad_fixtures)),
                        'stale-alloc-dso-source'):
    return 1

  bad_fixtures = dict(fixtures)
  bad_fixtures['src/plugin/Makefile.in'] = '__d_libdir__libdmtcp_alloc_so_SOURCES = alloc/mallocwrappers.cpp\n'
  if not expect_failure('stale plugin Makefile DSO',
                        lambda r: validate_static(r, Reader(bad_fixtures)),
                        'stale-alloc-dso-source'):
    return 1

  bad_fixtures = dict(fixtures)
  bad_fixtures['src/plugin/alloc/mallocwrappers.cpp'] = bad_fixtures['src/plugin/alloc/mallocwrappers.cpp'].replace('DMTCP_ALLOC_PLUGIN', 'DMTCP_ALLOC_DISABLED')
  if not expect_failure('missing predicate env marker',
                        lambda r: validate_wrappers(r, Reader(bad_fixtures)),
                        'alloc-enable-predicate'):
    return 1

  bad_fixtures = dict(fixtures)
  bad_fixtures['src/plugin/alloc/mallocwrappers.cpp'] = bad_fixtures['src/plugin/alloc/mallocwrappers.cpp'].replace('if (!dmtcp_alloc_enabled()) return _real_malloc(size); ', '')
  if not expect_failure('missing malloc fast-pass',
                        lambda r: validate_wrappers(r, Reader(bad_fixtures)),
                        'malloc-wrapper-fast-pass'):
    return 1

  bad_fixtures = dict(fixtures)
  bad_fixtures['src/pluginmanager.cpp'] = 'dmtcp_register_plugin(dmtcp_Alloc_PluginDescr());\n'
  if not expect_failure('synthetic alloc descriptor',
                        lambda r: validate_static(r, Reader(bad_fixtures)),
                        'alloc-descriptor-absence'):
    return 1

  bad = Result(emit=False)
  validate_static_scope(bad, Reader(fixtures), STATIC_INPUTS + ('.gsd/hidden-fixture.cpp',))
  if not any(contract == 'static-input-scope' for contract, _msg, _details in bad.failures):
    print('FAIL self-test: hidden-path static scope fixture was not rejected')
    return 1

  bad_fixtures = dict(fixtures)
  bad_fixtures['test/Makefile'] = '# target intentionally missing\n'
  if not expect_failure('missing probe target wiring',
                        lambda r: validate_test_wiring(r, Reader(bad_fixtures)),
                        'probe-make-target'):
    return 1

  print('PASS self-test: alloc built-in gate accepts local ENV_VAR fallbacks and rejects stale DSO tokens, missing predicates, synthetic descriptors, hidden static scopes, and missing probe target wiring')
  return 0

def main(argv):
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument('mode', choices=('static', 'wrappers', 'artifacts', 'runtime', 'self-test', 'full'))
  args = parser.parse_args(argv)

  if args.mode == 'self-test':
    return self_test()

  result = Result()
  reader = Reader()

  if args.mode in ('static', 'full'):
    validate_static(result, reader)
  if args.mode in ('wrappers', 'full'):
    validate_wrappers(result, reader)
  if args.mode in ('artifacts', 'full'):
    validate_artifacts(result)
  if args.mode in ('runtime', 'full'):
    validate_runtime(result)

  return result.finish()

if __name__ == '__main__':
  sys.exit(main(sys.argv[1:]))
