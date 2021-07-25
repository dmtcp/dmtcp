#!/usr/bin/python

# To try this out, do:
#   python <THIS_FILE> generate-example.txt

import sys
import os
import re

if len(sys.argv) != 2:
  print("***  Usage: " + sys.argv[0] + " <SPLIT_PROCESS.decl>")
  print("***         " + sys.argv[0] + " -  # Read declaratsion from stdin")
  print("***    " + "<SPLIT_PROCESS.decl> has lines like: void foo(int x);")
  sys.exit(1)


if sys.argv[-1] == "-":
  declarations_file = sys.stdin
  header_file = sys.stdout
else:
  declarations_file = open(sys.argv[1])

declarations = declarations_file.read().split(';')[:-1]  # Each decl ends in ';'
declarations_file.close()

# =============================================================

def abort_decl(decl, comment):
  print("*** Can't parse:  " + decl + " (" + comment + ")")
  sys.exit(1)
def get_var(arg):
  global var_idx
  words = re.split("[^a-zA-Z0-9_]+", arg.strip())
  if not words:
    abort_decl(arg, "arguments of a function declaration")
  var = words[-1] or words[-2]  # args[-1] might be empty string: int foo(int *)
  keyword = (len(words) >= 3 and not words[-1] and words[-3] or
             len(words) == 2 and words[-2])
  # if this is only a type, no var
  if (not re.match("[a-zA-Z0-9_]", var[-1]) or
      keyword in ["struct", "enum", "union"] or
      ((words[-1] or words[-2])
       in ["int", "unsigned", "signed", "float", "double", "char"])):
    var = "var" + str(var_idx)
  var_idx += 1 # increment varX for each arg position
  return var

def add_anonymous_vars_to_decl(decl, args, arg_vars):
  raw_args = []
  for (arg, var) in zip(args.split(','), arg_vars):
    if not re.match(r"\b" + var + r"\b", arg):  # if var not user-named variable
      assert re.match(r"\bvar[1-9]\b", var)
      arg += " " + var # then var must be a missing variable in decl; add it
    raw_args += [arg]
  return decl.split('(')[0] + "(" + ','.join(raw_args) + ")"

def emit_wrapper(decl, ret_type, fnc, args, arg_vars):
  if re.match(r"var[1-9]\b", ' '.join(arg_vars)):
    decl = add_anonymous_vars_to_decl(decl, args, arg_vars);
  # if arg_vars contains "varX", then "var2" needs to be inserted before
  # the second comma (or before the trailing ')' for 2-arg fnc)
  fargs = ""
  cargs = ""
  for (i, arg) in enumerate(args.split(',')):
    typ = ""
    iden = ""
    # try:
    types = arg.split(' ')
    iden = types[-1]
    assert '*' not in iden
    typ = " ".join(types[:-1])
    # except:
    #   print("Error:")
    #   print(fnc)
    #   print(arg)
    #   sys.exit(0);
    if len(iden.strip()) > 0 and len(typ.strip()) > 0:
      fargs += "%s%s %s, " % (typ, '' if '*' in typ else '*', iden)
      cargs += "%s%s, " % ('' if '*' in typ else '*', iden)
  # add ierr as the last argument for Fortran
  fargs += "int *ierr"
  if cargs.endswith(", "):
    cargs = cargs[:-2]
  
  if fargs == '' and not(fnc in ['MPI_Wtime', 'MPI_Wtick']):
    print("EXTERNC " + ret_type + " " + fnc.lower() + "_ (int* ierr) {")
  else:
    print("EXTERNC " + ret_type + " " + fnc.lower() + "_ (" + fargs + ") {")
  if fnc in ['MPI_Wtime', 'MPI_Wtick']:
    print("  return " + fnc + "(" + cargs + ");")
  else:
      print("  *ierr = " + fnc + "(" + cargs + ");")
      print("  return *ierr;")
  print("}")
  # print(ret_type + " " + fnc.lower() + "_ (" + args + ") __attribute__ ((weak, alias (\"" + fnc + "\")));")

# FIXME: declare local variables argc and argv,
#        get values from /proc/self/cmdline.
def emit_mpi_init():
  print("EXTERNC int mpi_init_ (int *ierr) {")
  print("  int argc = 0;")
  print("  char **argv;")
  print("  *ierr = MPI_Init(&argc, &argv);")
  print("  return *ierr;")
  print("}")

# FIXME: declare local variables argc and argv,
#        get values from /proc/self/cmdline.
def emit_mpi_init_thread():
  print("EXTERNC int mpi_init_thread_ (int* required, int* provided, int *ierr) {")
  print("  int argc = 0;")
  print("  char **argv;")
  print("  *ierr = MPI_Init_thread(&argc, &argv, *required, provided);")
  print("  return *ierr;")
  print("}")

for decl in declarations:
  # check for header file
  decl_oneline = re.sub('\n *', ' ', decl).strip()
  if decl_oneline.startswith("#"):
    print(decl_oneline.rstrip(';'))
    continue

  if decl.rstrip()[-1] != ')':
    abort_decl(decl, "missing final ')'")
  if '(' not in decl:
    abort_decl(decl, "missing '('")
#  decl_oneline = re.sub('\n *', ' ', decl).strip()
  (ret_type_and_fnc, args) = decl_oneline[:-1].split('(', 1)

  var_idx = 1
  fnc = get_var(ret_type_and_fnc)
  ret_type = ret_type_and_fnc.rstrip().rsplit(fnc, 1)[0].strip()

  var_idx = 1
  if args.strip(): # if one or more arguments
    arg_vars = [get_var(arg) for arg in args.split(',')]
  else:  # else this is a function of zero arguments
    arg_vars = []

  emit_wrapper(decl_oneline, ret_type, fnc, args, arg_vars)
  print("")  # emit a newline
emit_mpi_init()
emit_mpi_init_thread()
