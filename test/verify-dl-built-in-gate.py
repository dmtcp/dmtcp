#!/usr/bin/env python3
"""Executable gate for making the DL wrappers a libdmtcp.so built-in.

Modes:
  self-test  inline positive/negative fixtures for the gate itself
  wrappers   dmtcp_dl_enabled() predicate and dlopen/dlclose fast-pass checks
  static     source/config/test checks for the final DL built-in state
  artifacts  built-output stale internal DSO checks
  runtime    run launch/exec dlopen probes in normal, disabled, user-preload,
             and external-plugin modes
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

DL_SOURCES = ('src/plugin/dl/dlwrappers.cpp',)
DL_MAKE_SOURCES = tuple(p.replace('src/', '', 1) for p in DL_SOURCES)
DL_OBJECTS = tuple(p.replace('.cpp', '.$(OBJEXT)') for p in DL_MAKE_SOURCES)
DL_DEPFILES = tuple(
  p.rsplit('/', 1)[0] + '/$(DEPDIR)/' +
  p.rsplit('/', 1)[1].replace('.cpp', '.Po')
  for p in DL_MAKE_SOURCES
)

STATIC_INPUTS = (
  'include/dmtcp.h',
  'src/constants.h',
  'src/Makefile.am', 'src/Makefile.in', 'src/Makefile',
  'src/plugin/Makefile.am', 'src/plugin/Makefile.in', 'src/plugin/Makefile',
  'src/dmtcp_launch.cpp', 'src/pluginmanager.cpp',
) + DL_SOURCES

TEST_INPUTS = (
  'test/Makefile.in', 'test/Makefile',
  'test/dl-built-in-preload.c', 'test/dl-built-in-user-preload.c',
)

PROBE = 'test/dl-built-in-preload'
PROBE_SOURCE = 'test/dl-built-in-preload.c'
USER_PRELOAD_LIB = 'test/libdl-built-in-user-preload.so'
USER_PRELOAD_SOURCE = 'test/dl-built-in-user-preload.c'
EXTERNAL_PLUGIN_LIB = 'test/libdmtcp_plugin-init.so'
HELPER_LIBS = ('test/libdlopen-lib1.so', 'test/libdlopen-lib2.so')
AFTER_EXEC_ENV = 'DMTCP_DL_BUILT_IN_PRELOAD_AFTER_EXEC'

FORBIDDEN_DL_DSO = ('libdmtcp_dl.so', 'libdmtcp_dl')
REMOVED_INTERNAL_DSOS = (
  'libdmtcp_alloc', 'libdmtcp_dl', 'libdmtcp_ipc',
  'libdmtcp_svipc', 'libdmtcp_timer', 'libdmtcp_pid',
)

DL_ACCESSOR_PATTERNS = (
  'dmtcp_Dl_PluginDescr', 'dmtcp_DL_PluginDescr',
  'dmtcp_DlPluginDescr', 'dmtcp_DLPluginDescr',
  'Dl_PluginDescr', 'DL_PluginDescr',
  'DlPluginDescr', 'DLPluginDescr', 'dlPluginDescr',
)
PUBLIC_FORBIDDEN_PATTERNS = DL_ACCESSOR_PATTERNS + ('dmtcp_dl_enabled',)
DL_DESCRIPTOR_PATTERNS = DL_ACCESSOR_PATTERNS + (
  'DMTCP_DECL_PLUGIN', 'dmtcp_initialize_plugin', 'DmtcpPluginDescriptor_t',
)

NON_POD_STATIC_TYPES = (
  'string', 'vector', 'map', 'set', 'list', 'deque',
  'unordered_map', 'unordered_set',
)
UNSUPPORTED_PROOF_CLAIMS = (
  'TSAN', 'ThreadSanitizer', 'dlsym(RTLD_NEXT)', 'dl_iterate_phdr',
)
RUNTIME_MODE_NAMES = (
  'normal', 'disable-dl-flag', 'disable-dl-env',
  'disable-all', 'user-preload', 'external-plugin',
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
      result.fail(contract, 'required file is not readable',
                  ['{0}: {1}'.format(rel, exc)])
      return ''

  def read_static(self, rel, result, contract):
    if rel not in STATIC_INPUTS:
      result.fail('static-input-scope',
                  'static mode attempted to inspect an unapproved path', [rel])
      return ''
    return self.read_text(rel, result, contract)

  def read_test(self, rel, result, contract):
    if rel not in TEST_INPUTS:
      result.fail('test-input-scope',
                  'test mode attempted to inspect an unapproved path', [rel])
      return ''
    return self.read_text(rel, result, contract)


def strip_comments(text):
  def repl(match):
    return ''.join('\n' if ch == '\n' else ' ' for ch in match.group(0))
  text = re.sub(r'/\*.*?\*/', repl, text, flags=re.S)
  return re.sub(r'//.*', '', text)


def line_hits(text, needle):
  return ['line {0}: {1}'.format(i, line.strip())
          for i, line in enumerate(text.splitlines(), 1) if needle in line]


def make_var(text, name):
  lines = text.splitlines()
  for i, line in enumerate(lines):
    if line.startswith(name + ' =') or line.startswith(name + '='):
      block = [line]
      while block[-1].rstrip().endswith('\\') and i + 1 < len(lines):
        i += 1
        block.append(lines[i])
      return '\n'.join(block)
  return ''


def target_block(text, target):
  lines = text.splitlines()
  for i, line in enumerate(lines):
    if line.startswith(target + ':'):
      block = [line]
      j = i + 1
      while j < len(lines) and (lines[j].startswith('\t') or
                                lines[j].startswith(' ')):
        block.append(lines[j])
        j += 1
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


def contracts(result):
  return set(contract for contract, _msg, _details in result.failures)


def require_items(result, reader, rel, var, items, contract, label):
  block = make_var(reader.read_static(rel, result, contract), var)
  if not block:
    result.fail(contract, 'Makefile variable is missing',
                ['{0}: {1}'.format(rel, var)])
    return
  missing = [item for item in items if item not in block]
  if missing:
    result.fail(contract, 'Makefile variable is missing DL built-in entries',
                ['{0}: missing {1}'.format(rel, item) for item in missing])
    return
  result.pass_(contract, '{0}:{1} contains {2} DL {3}'.format(
    rel, var, len(items), label))


def require_absent(result, text, rel, needles, contract, msg):
  hits = []
  for needle in needles:
    for hit in line_hits(text, needle):
      hits.append('{0}: {1}'.format(rel, hit))
  if hits:
    result.fail(contract, msg, hits)
  else:
    result.pass_(contract, '{0} omits stale DL DSO tokens'.format(rel))


def validate_input_scope(result, reader, inputs=None):
  inputs = inputs or (STATIC_INPUTS + TEST_INPUTS)
  details = []
  seen = set()
  for rel in inputs:
    path = Path(rel)
    parts = path.parts
    if rel in seen:
      details.append('duplicate input: {0}'.format(rel))
    seen.add(rel)
    if path.is_absolute() or '..' in parts:
      details.append('non-repository-relative input: {0}'.format(rel))
    if not parts or parts[0] not in ('include', 'src', 'test'):
      details.append('input is outside include/src/test: {0}'.format(rel))
    if any(part in HIDDEN for part in parts):
      details.append('input uses hidden state: {0}'.format(rel))
    if path.suffix not in ('', '.am', '.c', '.cpp', '.h', '.in'):
      details.append('input suffix is not source/config/test-like: {0}'.format(rel))
    if reader.fixtures is None and not (REPO_ROOT / rel).is_file():
      details.append('input does not exist: {0}'.format(rel))
    if reader.fixtures is not None and rel not in reader.fixtures:
      details.append('fixture is missing: {0}'.format(rel))
  if details:
    result.fail('static-input-scope',
                'static/test input list violates the explicit source-only contract',
                details)
  else:
    result.pass_('static-input-scope',
                 'checks are limited to explicit include/src/test source and configuration files')


def validate_makefile_ownership(result, reader):
  for rel in ('src/Makefile.am', 'src/Makefile.in', 'src/Makefile'):
    require_items(result, reader, rel, '__d_libdir__libdmtcp_so_SOURCES',
                  DL_MAKE_SOURCES, 'makefile-libdmtcp-dl-sources', 'sources')
  for rel in ('src/Makefile.in', 'src/Makefile'):
    require_items(result, reader, rel, 'am___d_libdir__libdmtcp_so_OBJECTS',
                  DL_OBJECTS, 'makefile-libdmtcp-dl-objects', 'objects')
    require_items(result, reader, rel, 'am__depfiles_remade',
                  DL_DEPFILES, 'makefile-libdmtcp-dl-dependencies', 'depfiles')


def validate_removed_dl_dso_source(result, reader):
  for rel in ('src/plugin/Makefile.am', 'src/plugin/Makefile.in',
              'src/plugin/Makefile', 'src/dmtcp_launch.cpp'):
    require_absent(result, reader.read_static(rel, result, 'stale-dl-dso-source'),
                   rel, FORBIDDEN_DL_DSO, 'stale-dl-dso-source',
                   'stale DL DSO launcher/build token is present')


def validate_disable_dl_launch_contract(result, reader):
  text = strip_comments(reader.read_static('src/dmtcp_launch.cpp', result,
                                           'disable-dl-propagation'))
  details = []
  if '--disable-dl-plugin' not in text:
    details.append('usage/argument parsing no longer names --disable-dl-plugin')
  if 'DMTCP_DL_PLUGIN=[01]' not in text and 'ENV_VAR_DL_PLUGIN' not in text:
    details.append('usage/argument parsing no longer names DMTCP_DL_PLUGIN')
  if not re.search(r's\s*==\s*"--disable-dl-plugin"', text):
    details.append('processArgs() has no --disable-dl-plugin branch')
  if 'setenv(ENV_VAR_DL_PLUGIN, "0", 1)' not in text:
    details.append('--disable-dl-plugin branch does not set ENV_VAR_DL_PLUGIN=0')
  if 'getenv(ENV_VAR_DL_PLUGIN)' not in text:
    details.append('setLDPreloadLibs() no longer reads direct DMTCP_DL_PLUGIN')
  if 'strcmp(ptr, "0")' not in text or 'strcmp(ptr, "1")' not in text:
    details.append('direct DMTCP_DL_PLUGIN handling no longer preserves 0/1 values')
  if 'setenv(ENV_VAR_DISABLE_ALL_PLUGINS, disableAllPlugins ? "1" : "0", 1)' not in text:
    details.append('disable-all state is not propagated through ENV_VAR_DISABLE_ALL_PLUGINS')
  if details:
    result.fail('disable-dl-propagation',
                'launcher does not preserve disable-dl argument/env propagation',
                details)
  else:
    result.pass_('disable-dl-propagation',
                 '--disable-dl-plugin, DMTCP_DL_PLUGIN, and disable-all env propagation are preserved')


def validate_descriptor_absence(result, reader):
  details = []
  public_header = strip_comments(reader.read_static('include/dmtcp.h', result,
                                                    'dl-descriptor-absence'))
  pluginmanager = strip_comments(reader.read_static('src/pluginmanager.cpp', result,
                                                    'dl-descriptor-absence'))
  dl_source = strip_comments(reader.read_static('src/plugin/dl/dlwrappers.cpp', result,
                                                'dl-descriptor-absence'))
  for token in PUBLIC_FORBIDDEN_PATTERNS:
    if token in public_header:
      details.append('include/dmtcp.h: unexpected DL built-in API/accessor token {0}'.format(token))
  for token in DL_ACCESSOR_PATTERNS:
    if token in pluginmanager:
      details.append('src/pluginmanager.cpp: unexpected DL descriptor/accessor token {0}'.format(token))
  if re.search(r'dmtcp_register_plugin\s*\(\s*dmtcp_.*Dl', pluginmanager):
    details.append('src/pluginmanager.cpp: unexpected DL PluginManager registration row')
  for token in DL_DESCRIPTOR_PATTERNS:
    if token in dl_source:
      details.append('src/plugin/dl/dlwrappers.cpp: unexpected plugin descriptor/public initializer token {0}'.format(token))
  if details:
    result.fail('dl-descriptor-absence',
                'DL wrapper group acquired descriptor, public API, or PluginManager surface',
                details)
  else:
    result.pass_('dl-descriptor-absence',
                 'DL remains a wrapper-only built-in group with no descriptor or public accessor')


def validate_dl_predicate(result, reader):
  text = strip_comments(reader.read_static('src/plugin/dl/dlwrappers.cpp', result,
                                           'dl-enable-predicate'))
  public_header = strip_comments(reader.read_static('include/dmtcp.h', result,
                                                    'dl-enable-predicate'))
  details = []
  if 'dmtcp_dl_enabled' in public_header:
    details.append('include/dmtcp.h: dmtcp_dl_enabled leaked into public plugin ABI')
  body = function_body(text, 'dmtcp_dl_enabled')
  if body is None:
    details.append('src/plugin/dl/dlwrappers.cpp: missing private dmtcp_dl_enabled() definition')
  else:
    env_markers = (
      ('DMTCP_DL_PLUGIN', 'ENV_VAR_DL_PLUGIN'),
      ('DMTCP_DISABLE_ALL_PLUGINS', 'ENV_VAR_DISABLE_ALL_PLUGINS'),
    )
    for literal, macro in env_markers:
      if literal not in body and macro not in body:
        details.append('dmtcp_dl_enabled() missing {0} or {1} control'.format(literal, macro))
    if re.search(r'return\s+1\s*;', body) and not re.search(r'return\s+0\s*;', body):
      details.append('dmtcp_dl_enabled() appears unconditional')
    if 'static' in body:
      for nonpod in NON_POD_STATIC_TYPES:
        if re.search(r'\bstatic\s+(?:const\s+)?(?:(?:std|dmtcp)::)?' +
                     re.escape(nonpod) + r'\b', body):
          details.append('dmtcp_dl_enabled() uses non-POD static {0}'.format(nonpod))
  if details:
    result.fail('dl-enable-predicate',
                'DL enable predicate markers are missing, public, unconditional, or not static-init safe',
                details)
  else:
    result.pass_('dl-enable-predicate',
                 'private dmtcp_dl_enabled() is keyed by DL/disable-all controls without public ABI leakage')


def validate_wrapper_fast_pass(result, reader):
  text = strip_comments(reader.read_static('src/plugin/dl/dlwrappers.cpp', result,
                                           'dl-wrapper-fast-pass'))
  details = []
  wrappers = (
    ('dlopen', '_real_dlopen'),
    ('dlclose', '_real_dlclose'),
  )
  early_tokens = ('LibDlWrapperLock', 'getRpathRunPath', 'dlopen_try_paths',
                  'JTRACE', 'ASSERT_', 'JASSERT')
  for symbol, real_symbol in wrappers:
    body = function_body(text, symbol)
    label = 'src/plugin/dl/dlwrappers.cpp:{0}'.format(symbol)
    if body is None:
      details.append('{0}: missing wrapper definition'.format(label))
      continue
    pred_idx = body.find('dmtcp_dl_enabled')
    real_idx = body.find(real_symbol)
    compact = re.sub(r'\s+', ' ', body)
    fast_re = (r'if\s*\(\s*!\s*dmtcp_dl_enabled\s*\(\s*\)\s*\)\s*'
               r'(?:\{\s*)?return\s+' + re.escape(real_symbol) + r'\s*\(')
    if not re.search(fast_re, compact):
      details.append('{0}: missing disabled fast-pass return to {1}'.format(label, real_symbol))
    if real_idx == -1:
      details.append('{0}: missing {1} call'.format(label, real_symbol))
    if pred_idx == -1:
      details.append('{0}: missing dmtcp_dl_enabled() check'.format(label))
    else:
      for token in early_tokens:
        idx = body.find(token)
        if idx != -1 and idx < pred_idx:
          details.append('{0}: {1} can run before disabled fast-pass'.format(label, token))
  if details:
    result.fail('dl-wrapper-fast-pass',
                'dlopen/dlclose disabled fast-pass markers are missing or misplaced',
                details)
  else:
    result.pass_('dl-wrapper-fast-pass',
                 'dlopen and dlclose fast-pass to real libdl before DMTCP DL bookkeeping when disabled')


def validate_wrappers(result, reader):
  validate_dl_predicate(result, reader)
  validate_wrapper_fast_pass(result, reader)


def validate_probe_target(result, reader):
  for rel in ('test/Makefile.in', 'test/Makefile'):
    text = reader.read_test(rel, result, 'probe-make-target')
    probe_block = target_block(text, 'dl-built-in-preload')
    user_block = target_block(text, 'libdl-built-in-user-preload.so')
    if not probe_block:
      result.fail('probe-make-target', 'DL preload probe target is missing', [rel])
    else:
      missing = [token for token in ('dl-built-in-preload.c',
                                     'libdlopen-lib1.so',
                                     'libdlopen-lib2.so', '-ldl')
                 if token not in probe_block]
      if missing:
        result.fail('probe-make-target',
                    'DL preload probe target does not reuse helper libs or link libdl',
                    ['{0}: missing {1}'.format(rel, token) for token in missing])
      else:
        result.pass_('probe-make-target',
                     '{0} builds dl-built-in-preload with libdlopen helpers and -ldl'.format(rel))
    if not user_block:
      result.fail('user-preload-make-target',
                  'DL user-preload helper target is missing', [rel])
    else:
      missing = [token for token in ('dl-built-in-user-preload.c', '-shared', '-fPIC')
                 if token not in user_block]
      if missing:
        result.fail('user-preload-make-target',
                    'DL user-preload helper target does not build a shared object',
                    ['{0}: missing {1}'.format(rel, token) for token in missing])
      else:
        result.pass_('user-preload-make-target',
                     '{0} builds libdl-built-in-user-preload.so as a shared object'.format(rel))
    tests_block = make_var(text, 'TESTS')
    if not tests_block:
      result.fail('user-preload-not-standalone-test',
                  'test Makefile TESTS variable is missing', [rel])
    elif re.search(r'(^|\s)dl-built-in-user-preload(\s|\\|$)', tests_block):
      result.fail('user-preload-not-standalone-test',
                  'DL user-preload helper must not be built as a standalone TESTS executable',
                  ['{0}: {1}'.format(rel, tests_block)])
    elif '$(srcdir)/*.c' in tests_block and 'dl-built-in-user-preload.c' not in tests_block:
      result.fail('user-preload-not-standalone-test',
                  'wildcard TESTS source list must explicitly exclude the user-preload helper source',
                  ['{0}: {1}'.format(rel, tests_block)])
    else:
      result.pass_('user-preload-not-standalone-test',
                   '{0} keeps dl-built-in-user-preload out of standalone TESTS builds'.format(rel))


def validate_probe_source(result, reader):
  source = reader.read_test(PROBE_SOURCE, result, 'probe-source-contract')
  required = (
    'PASS dl-built-in-preload:', 'mode=', 'LD_PRELOAD',
    'DMTCP_HIJACK_LIBS', 'DMTCP_HIJACK_LIBS_M32',
    'DMTCP_ORIG_LD_PRELOAD', 'DMTCP_PLUGIN', 'DMTCP_DL_PLUGIN',
    'DMTCP_DISABLE_ALL_PLUGINS', AFTER_EXEC_ENV,
    'libdmtcp.so', 'libdmtcp_dl.so', 'libdlopen-lib1.so',
    'libdl-built-in-user-preload.so', 'libdmtcp_plugin-init.so',
    '/proc/self/maps', 'dlopen(NULL', 'dlsym', 'fnc', 'dlclose', 'execvp',
  )
  missing = [token for token in required if token not in source]
  if missing:
    result.fail('probe-source-contract',
                'probe source is missing required diagnostic/runtime token', missing)
  else:
    result.pass_('probe-source-contract',
                 'probe source checks bounded env, maps, dlopen/dlsym/dlclose, user preload, external plugin, and exec reconstruction')
  claim_hits = [token for token in UNSUPPORTED_PROOF_CLAIMS if token in source]
  if claim_hits:
    result.fail('unsupported-runtime-proof-claim',
                'probe source contains unsupported sanitizer/deep-dlsym proof claim',
                claim_hits)
  else:
    result.pass_('unsupported-runtime-proof-claim',
                 'probe avoids unsupported TSAN, RTLD_NEXT, and dl_iterate_phdr proof claims')


def validate_user_preload_source(result, reader):
  source = reader.read_test(USER_PRELOAD_SOURCE, result, 'user-preload-source-contract')
  required = ('dmtcp_dl_built_in_user_preload_marker', '__attribute__((constructor))')
  missing = [token for token in required if token not in source]
  if missing:
    result.fail('user-preload-source-contract',
                'user preload helper is missing marker/constructor coverage', missing)
  else:
    result.pass_('user-preload-source-contract',
                 'user preload helper exports a marker and constructor-visible state')


def validate_runtime_submode_plan(result):
  missing = [name for name in ('user-preload', 'external-plugin')
             if name not in RUNTIME_MODE_NAMES]
  if missing:
    result.fail('runtime-submode-plan',
                'runtime mode would silently skip required submodes', missing)
  else:
    result.pass_('runtime-submode-plan',
                 'runtime mode includes normal, disabled, user-preload, and external-plugin submodes')


def validate_test_wiring(result, reader):
  validate_probe_target(result, reader)
  validate_probe_source(result, reader)
  validate_user_preload_source(result, reader)
  validate_runtime_submode_plan(result)


def validate_static(result, reader):
  validate_input_scope(result, reader)
  validate_makefile_ownership(result, reader)
  validate_removed_dl_dso_source(result, reader)
  validate_disable_dl_launch_contract(result, reader)
  validate_descriptor_absence(result, reader)
  validate_test_wiring(result, reader)


def validate_artifacts(result):
  lib_dir = REPO_ROOT / 'lib'
  offenders = []
  if not lib_dir.exists():
    result.fail('artifacts-lib-output',
                'artifacts mode requires generated lib/ outputs; run a build before this mode',
                ['lib/'])
  else:
    for path in sorted(lib_dir.rglob('*')):
      rel = str(path.relative_to(REPO_ROOT))
      if any(token in rel for token in REMOVED_INTERNAL_DSOS):
        offenders.append(rel)
      if path.is_symlink():
        try:
          target = os.readlink(str(path))
        except OSError:
          target = ''
        if any(token in target for token in REMOVED_INTERNAL_DSOS):
          offenders.append('{0} -> {1}'.format(rel, target))
    if offenders:
      result.fail('artifacts-lib-output',
                  'generated lib outputs contain removed internal DSO artifact(s)',
                  offenders)
    else:
      result.pass_('artifacts-lib-output',
                   'generated lib outputs contain no removed internal DSO artifacts')

  launcher = REPO_ROOT / 'bin/dmtcp_launch'
  if not launcher.exists():
    result.fail('artifacts-launcher-strings',
                'artifacts mode requires built bin/dmtcp_launch; run a build before this mode',
                ['bin/dmtcp_launch'])
    return
  try:
    data = launcher.read_bytes()
  except OSError as exc:
    result.fail('artifacts-launcher-strings',
                'could not read built dmtcp_launch binary', [str(exc)])
    return
  hits = [token for token in REMOVED_INTERNAL_DSOS if token.encode('utf-8') in data]
  if hits:
    result.fail('artifacts-launcher-strings',
                'built dmtcp_launch still embeds removed internal DSO string(s)',
                hits)
  else:
    result.pass_('artifacts-launcher-strings',
                 'built dmtcp_launch embeds no removed internal DSO strings')


def base_runtime_env():
  env = os.environ.copy()
  for key in (
      'LD_PRELOAD', 'DMTCP_HIJACK_LIBS', 'DMTCP_HIJACK_LIBS_M32',
      'DMTCP_ORIG_LD_PRELOAD', 'DMTCP_PLUGIN', 'DMTCP_DL_PLUGIN',
      'DMTCP_DISABLE_ALL_PLUGINS', AFTER_EXEC_ENV):
    env.pop(key, None)
  return env


def run_probe(result, contract, args, env=None, timeout=60):
  try:
    proc = subprocess.run(args, cwd=str(REPO_ROOT), text=True, env=env,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          timeout=timeout, check=False)
  except subprocess.TimeoutExpired as exc:
    output = (exc.stdout or '') + (exc.stderr or '')
    result.fail(contract, 'DL preload runtime probe timed out',
                ['command: {0}'.format(' '.join(args)),
                 'output: {0}'.format(output[-2000:])])
    return
  if proc.returncode != 0:
    details = ['command: {0}'.format(' '.join(args)),
               'exit: {0}'.format(proc.returncode)]
    if proc.stdout.strip():
      details.append('stdout: {0}'.format(proc.stdout.strip()[-2000:]))
    if proc.stderr.strip():
      details.append('stderr: {0}'.format(proc.stderr.strip()[-2000:]))
    result.fail(contract, 'DL preload runtime probe failed', details)
  else:
    msg = proc.stdout.strip().splitlines()[-1] if proc.stdout.strip() else 'exit 0'
    result.pass_(contract, msg)


def validate_runtime(result):
  launcher = REPO_ROOT / 'bin/dmtcp_launch'
  probe = REPO_ROOT / PROBE
  base_required = [('runtime-probe-launcher', launcher),
                   ('runtime-probe-built', probe)]
  missing_base = ['{0}: {1}'.format(contract, path.relative_to(REPO_ROOT))
                  for contract, path in base_required if not path.exists()]
  missing_helpers = [rel for rel in HELPER_LIBS if not (REPO_ROOT / rel).exists()]
  if missing_base or missing_helpers:
    result.fail('runtime-required-artifacts',
                'runtime mode requires built launcher, probe, and dlopen helper libraries',
                missing_base + missing_helpers)
    return
  if not os.access(str(probe), os.X_OK):
    result.fail('runtime-required-artifacts',
                'DL preload probe is not executable', [PROBE])
    return
  result.pass_('runtime-required-artifacts',
               'dmtcp_launch, DL probe, and libdlopen helpers exist')

  run_probe(result, 'runtime-probe-normal',
            ['bin/dmtcp_launch', '--coord-port', '0', '--quiet', './' + PROBE, 'normal'],
            env=base_runtime_env())
  run_probe(result, 'runtime-probe-disable-dl-flag',
            ['bin/dmtcp_launch', '--coord-port', '0', '--quiet', '--disable-dl-plugin',
             './' + PROBE, 'disable-dl-flag'],
            env=base_runtime_env())
  env = base_runtime_env()
  env['DMTCP_DL_PLUGIN'] = '0'
  run_probe(result, 'runtime-probe-disable-dl-env',
            ['bin/dmtcp_launch', '--coord-port', '0', '--quiet', './' + PROBE, 'disable-dl-env'],
            env=env)
  run_probe(result, 'runtime-probe-disable-all',
            ['bin/dmtcp_launch', '--coord-port', '0', '--quiet', '--disable-all-plugins',
             './' + PROBE, 'disable-all'],
            env=base_runtime_env())

  user_lib = REPO_ROOT / USER_PRELOAD_LIB
  if not user_lib.exists():
    result.fail('runtime-probe-user-preload',
                'runtime mode must not skip user-preload submode; helper is missing',
                [USER_PRELOAD_LIB])
  else:
    env = base_runtime_env()
    env['LD_PRELOAD'] = str(user_lib)
    run_probe(result, 'runtime-probe-user-preload',
              ['bin/dmtcp_launch', '--coord-port', '0', '--quiet', './' + PROBE, 'user-preload'],
              env=env)

  plugin_lib = REPO_ROOT / EXTERNAL_PLUGIN_LIB
  if not plugin_lib.exists():
    result.fail('runtime-probe-external-plugin',
                'runtime mode must not skip external-plugin submode; plugin is missing',
                [EXTERNAL_PLUGIN_LIB])
  else:
    run_probe(result, 'runtime-probe-external-plugin',
              ['bin/dmtcp_launch', '--coord-port', '0', '--quiet', '--with-plugin', str(plugin_lib),
               './' + PROBE, 'external-plugin'],
              env=base_runtime_env())


def positive_fixtures():
  dl_source = r'''
#include <stdlib.h>
#include <string.h>
#define _real_dlopen NEXT_FNC(dlopen)
#define _real_dlclose NEXT_FNC(dlclose)
static int
dmtcp_dl_enabled()
{
  static int cached = -1;
  const char *dl = getenv("DMTCP_DL_PLUGIN");
  const char *all = getenv("DMTCP_DISABLE_ALL_PLUGINS");
  if ((dl != NULL && strcmp(dl, "0") == 0) ||
      (all != NULL && strcmp(all, "1") == 0)) return 0;
  cached = 1;
  return cached;
}
extern "C" void *dlopen(const char *filename, int flag)
{
  if (!dmtcp_dl_enabled()) return _real_dlopen(filename, flag);
  LibDlWrapperLock wrapperLock;
  char rpath[4096]; char runpath[4096];
  getRpathRunPath(__builtin_return_address(0), rpath, runpath);
  void *ret = dlopen_try_paths(filename, flag, runpath);
  if (ret == NULL) { JTRACE("dlopen failed"); }
  return ret;
}
extern "C" int dlclose(void *handle)
{
  if (!dmtcp_dl_enabled()) return _real_dlclose(handle);
  LibDlWrapperLock wrapperLock;
  return _real_dlclose(handle);
}
'''
  launch = r'''
static const char *theUsage =
  "  --disable-dl-plugin: (environment variable DMTCP_DL_PLUGIN=[01])\n";
static bool disableAllPlugins = false;
static bool enableDlPlugin = true;
static struct PluginInfo pluginInfo[] = {
  { &enableLibDMTCP, "libdmtcp.so" }
};
static void processArgs(int *argc, const char ***argv) {
  string s = "--disable-dl-plugin";
  if (s == "--disable-dl-plugin") { setenv(ENV_VAR_DL_PLUGIN, "0", 1); }
}
static void setLDPreloadLibs(bool is32bitElf) {
  if (getenv(ENV_VAR_DL_PLUGIN) != NULL) {
    const char *ptr = getenv(ENV_VAR_DL_PLUGIN);
    if (strcmp(ptr, "1") == 0) { enableDlPlugin = true; }
    else if (strcmp(ptr, "0") == 0) { enableDlPlugin = false; }
  }
  setenv(ENV_VAR_DISABLE_ALL_PLUGINS, disableAllPlugins ? "1" : "0", 1);
}
'''
  src = '__d_libdir__libdmtcp_so_SOURCES = ' + ' '.join(DL_MAKE_SOURCES) + '\n'
  obj = 'am___d_libdir__libdmtcp_so_OBJECTS = ' + ' '.join(DL_OBJECTS) + '\n'
  dep = 'am__depfiles_remade = ' + ' '.join(DL_DEPFILES) + '\n'
  test_make = (
    "TESTS=${notdir ${basename ${shell ls $(srcdir)/*.c $(srcdir)/*.cpp $(srcdir)/*.cilk | grep -v -e 'hellompi.c' -e 'dl-built-in-user-preload.c'}}} \\\n"
    '\thellompich openmpi\n\n'
    'dl-built-in-preload: dl-built-in-preload.c libdlopen-lib1.so libdlopen-lib2.so\n'
    '\t$(CC) -o $@ $< $(CFLAGS) -ldl\n\n'
    'libdl-built-in-user-preload.so: dl-built-in-user-preload.c\n'
    '\t$(CC) $(CFLAGS) -shared -fPIC -o $@ $<\n'
  )
  probe = (
    'PASS dl-built-in-preload: mode= LD_PRELOAD DMTCP_HIJACK_LIBS '
    'DMTCP_HIJACK_LIBS_M32 DMTCP_ORIG_LD_PRELOAD DMTCP_PLUGIN '
    'DMTCP_DL_PLUGIN DMTCP_DISABLE_ALL_PLUGINS ' + AFTER_EXEC_ENV + ' '
    'libdmtcp.so libdmtcp_dl.so libdlopen-lib1.so '
    'libdl-built-in-user-preload.so libdmtcp_plugin-init.so /proc/self/maps '
    'dlopen(NULL dlsym fnc dlclose execvp\n'
  )
  helper = 'void __attribute__((constructor)) init(void) {}\nint dmtcp_dl_built_in_user_preload_marker(void) { return 4242; }\n'
  return {
    'include/dmtcp.h': '/* public ABI intentionally omits DL built-in accessors */\n',
    'src/constants.h': '#define ENV_VAR_DL_PLUGIN "DMTCP_DL_PLUGIN"\n#define ENV_VAR_DISABLE_ALL_PLUGINS "DMTCP_DISABLE_ALL_PLUGINS"\n',
    'src/Makefile.am': src,
    'src/Makefile.in': src + obj + dep,
    'src/Makefile': src + obj + dep,
    'src/plugin/Makefile.am': 'no stale dso target here\n',
    'src/plugin/Makefile.in': 'no stale dso target here\n',
    'src/plugin/Makefile': 'no stale dso target here\n',
    'src/dmtcp_launch.cpp': launch,
    'src/pluginmanager.cpp': 'dmtcp_register_plugin(dmtcp_Core_PluginDescr());\n',
    'src/plugin/dl/dlwrappers.cpp': dl_source,
    'test/Makefile.in': test_make,
    'test/Makefile': test_make,
    PROBE_SOURCE: probe,
    USER_PRELOAD_SOURCE: helper,
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
  validate_wrappers(ok, reader)
  if ok.failures:
    print('FAIL self-test: positive fixture unexpectedly failed')
    for contract, msg, details in ok.failures:
      print('  - {0}: {1}: {2}'.format(contract, msg, '; '.join(details)))
    return 1

  bad = dict(fixtures)
  bad['src/dmtcp_launch.cpp'] = bad['src/dmtcp_launch.cpp'] + '"libdmtcp_dl.so";\n'
  bad['src/plugin/Makefile.in'] = 'libdmtcp_PROGRAMS += $(d_libdir)/libdmtcp_dl.so\n'
  if not expect_failure('stale DL DSO launcher/build token',
                        lambda r: validate_static(r, Reader(bad)),
                        'stale-dl-dso-source'):
    return 1

  bad_result = Result(emit=False)
  validate_input_scope(bad_result, Reader(fixtures),
                       STATIC_INPUTS + ('.gsd/hidden-fixture.cpp',))
  if 'static-input-scope' not in contracts(bad_result):
    print('FAIL self-test: hidden-path fixture was not rejected')
    return 1
  bad_result = Result(emit=False)
  validate_input_scope(bad_result, Reader(fixtures),
                       STATIC_INPUTS + ('/tmp/absolute-fixture.cpp',))
  if 'static-input-scope' not in contracts(bad_result):
    print('FAIL self-test: absolute-path fixture was not rejected')
    return 1

  bad = dict(fixtures)
  bad['src/plugin/dl/dlwrappers.cpp'] = bad['src/plugin/dl/dlwrappers.cpp'].replace('dmtcp_dl_enabled', 'dmtcp_dl_missing')
  if not expect_failure('missing predicate',
                        lambda r: validate_wrappers(r, Reader(bad)),
                        'dl-enable-predicate'):
    return 1

  bad = dict(fixtures)
  bad['src/plugin/dl/dlwrappers.cpp'] = bad['src/plugin/dl/dlwrappers.cpp'].replace(
    'if (!dmtcp_dl_enabled()) return _real_dlopen(filename, flag);\n  ', '')
  if not expect_failure('missing dlopen fast-pass',
                        lambda r: validate_wrappers(r, Reader(bad)),
                        'dl-wrapper-fast-pass'):
    return 1

  bad = dict(fixtures)
  bad['src/pluginmanager.cpp'] = 'dmtcp_register_plugin(dmtcp_Dl_PluginDescr());\n'
  bad['src/plugin/dl/dlwrappers.cpp'] += 'DMTCP_DECL_PLUGIN(dmtcp_Dl_PluginDescr());\n'
  if not expect_failure('synthetic DL descriptor',
                        lambda r: validate_static(r, Reader(bad)),
                        'dl-descriptor-absence'):
    return 1

  bad = dict(fixtures)
  bad['test/Makefile'] = bad['test/Makefile'].replace(
    'libdl-built-in-user-preload.so: dl-built-in-user-preload.c\n\t$(CC) $(CFLAGS) -shared -fPIC -o $@ $<\n', '')
  if not expect_failure('missing user-preload target wiring',
                        lambda r: validate_test_wiring(r, Reader(bad)),
                        'user-preload-make-target'):
    return 1

  bad = dict(fixtures)
  bad['test/Makefile'] = bad['test/Makefile'].replace(
    " -e 'dl-built-in-user-preload.c'", '')
  if not expect_failure('user-preload helper included in wildcard TESTS',
                        lambda r: validate_test_wiring(r, Reader(bad)),
                        'user-preload-not-standalone-test'):
    return 1

  bad = dict(fixtures)
  bad[PROBE_SOURCE] += 'Unsupported TSAN proof using dlsym(RTLD_NEXT) and dl_iterate_phdr.\n'
  if not expect_failure('unsupported runtime proof claim',
                        lambda r: validate_probe_source(r, Reader(bad)),
                        'unsupported-runtime-proof-claim'):
    return 1

  print('PASS self-test: DL built-in gate rejects stale DSO tokens, hidden/absolute paths, missing predicate/fast-pass markers, synthetic descriptors, missing user-preload wiring, wildcard TESTS helper inclusion, and unsupported TSAN/RTLD_NEXT/dl_iterate_phdr claims')
  return 0


def main(argv):
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument('mode', choices=('self-test', 'wrappers', 'static',
                                       'artifacts', 'runtime', 'full'))
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
