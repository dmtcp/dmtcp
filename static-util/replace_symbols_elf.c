// elf..
#include <elf.h>

// write, getopt..
#include <getopt.h>
#include <unistd.h>

// printf..
#include <stdio.h>

// exit..
#include <stdlib.h>

// assert
#include <assert.h>

// c strings..
#include <string.h>

// for open..
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

// utilities
#include "util.h"

// MACROS
#define c_print(x) write(1, x, strlen(x))

// CONST
const int debug = 0;
const int debug_func = 0;

// ENUMERATION
typedef enum { SINGLESYM = 0, KEEPNUMSYM, COMPLETESYM, STARTWITHSYM } FLAGTYPE;

typedef enum { BOTH_DEF_AND_UNDEF = 0, ONLY_DEF, ONLY_UNDEF } REPLACETYPE;

static REPLACETYPE def_or_undef = BOTH_DEF_AND_UNDEF;
static int verbose = 0;

static char *actualSingleSymbolsToReplace[300] = {0};
static int actualSingleSymbolsIndex = 0;

static char *actualKeepNumSymbolsToReplace[300] = {0};
static int actualKeepNumSymbolsIndex = 0;

static char *actualCompleteSymbolsToReplace[300] = {0};
static int actualCompleteSymbolsIndex = 0;

static char *actualStartWithSymbolsToReplace[300] = {0};
static int actualStartWithSymbolsIndex = 0;

// Future function implementations:
int readInElfHeader();
int readInSymbolTable();
int readInStringTable();
int readInSectionHeaderTable();

int writeOutElfHeader();
int writeOutSymbolTable();
int writeOutStringTable();
int wrtieOutSectionHeaderTable();

// Helper functions
int runGetOpt(int argc, char **argv, int *objIndex, char **objList,
              int *singleSymbolIndex, char **singleSymbolList,
              int *keepNumSymbolIndex, char **keepNumSymbolList,
              int *completeSymbolIndex, char **completeSymbolList,
              int *startWithSymbolIndex, char **startWithSymbolList,
              char **singleStrPtr, char **keepNumStrPtr, char **completeStrPtr,
              char **startWithStrPtr) {
  if (debug_func)
    printf("runGetOpt\n");
  int c;
  int digit_optind = 0;
  while (1) {
    int this_option_optind = optind ? optind : 1;
    int option_index = 0;
    static struct option long_options[] = {
        {"object", required_argument, 0, 0},
        {"singlesymbol", required_argument, 0, 1},
        {"keepnumsymbol", required_argument, 0, 2},
        {"singlestr", required_argument, 0, 3},
        {"keepnumstr", required_argument, 0, 4},
        {"completesymbol", required_argument, 0, 7},
        {"completestr", required_argument, 0, 8},
        {"only_def", no_argument, 0, 9},
        {"only_undef", no_argument, 0, 10},
        {"startwithsymbol", required_argument, 0, 11},
        {"startwithstr", required_argument, 0, 12},
        {"verbose", no_argument, 0, 'v'},
        {0, 0, 0, 0}};

    c = getopt_long(argc, argv, "o:s:k:f:c:v", long_options, &option_index);
    if (c == -1)
      break;

    //printf("option %s", long_options[option_index].name);
    //if (optarg)
    //  printf(" with arg %s", optarg);
    //printf("\n");

    switch (c) {
    case 0: // object
      objList[(*objIndex)++] = strdup(optarg);
      break;
    case 'o':
      optind--;
      do {
        objList[(*objIndex)++] = strdup(argv[optind]);
        optind++;
      } while (optind < argc && *argv[optind] != '-');
      break;

    case 1: // single symbol
      singleSymbolList[(*singleSymbolIndex)++] = strdup(optarg);
      break;
    case 's':
      optind--;
      do {
        singleSymbolList[(*singleSymbolIndex)++] = strdup(argv[optind]);
        optind++;
      } while (optind < argc && *argv[optind] != '-');
      break;

    case 2: // multiple symbol requiring numbering
      if (objIndex == 0) {
        printf("*** ***If need to assign numbering to symbol, list object "
               "files first!\n");
        perror("Syntax: ./replace-symbols-name -o <obj file> [obj files] "
               "--keepnumsymbol=<symbol>");
        exit(1);
      }
      keepNumSymbolList[(*keepNumSymbolIndex)++] = strdup(optarg);
      break;
    case 'k':
      if (objIndex == 0) {
        printf("*** ***If need to assign numbering to symbol, list object "
               "files first!\n");
        perror("Syntax: ./replace-symbols-name -o <obj file> [obj files] "
               "--keepnumsymbol=<symbol>");
        exit(1);
      }
      optind--;
      do {
        keepNumSymbolList[(*keepNumSymbolIndex)++] = strdup(argv[optind]);
        optind++;
      } while (optind < argc && *argv[optind] != '-');
      break;

    case 7: // complete symbol replacement
      completeSymbolList[(*completeSymbolIndex)++] = strdup(optarg);
      break;
    case 'c':
      optind--;
      do {
        completeSymbolList[(*completeSymbolIndex)++] = strdup(argv[optind]);
        optind++;
      } while (optind < argc && *argv[optind] != '-');
      break;

    case 11: // startwith symbol replacement
      startWithSymbolList[(*startWithSymbolIndex)++] = strdup(optarg);
      break;
    case 'f':
      optind--;
      do {
        startWithSymbolList[(*startWithSymbolIndex)++] = strdup(argv[optind]);
        optind++;
      } while (optind < argc && *argv[optind] != '-');
      break;

    case 3:
      if (*singleStrPtr) {
        free(*singleStrPtr);
        printf("*** ***Only use the flag --singestr=<symbol> once.\n");
        exit(1);
      }
      *singleStrPtr = strdup(optarg);
      break;
    case 4:
      if (*keepNumStrPtr) {
        free(*keepNumStrPtr);
        printf("*** ***Only use the flag --keepnumstr=<symbol> once.\n");
        exit(1);
      }
      *keepNumStrPtr = strdup(optarg);
      break;
    case 8:
      if (*completeStrPtr) {
        free(*completeStrPtr);
        printf("*** ***Only use the flag --completestr=<symbol> once.\n");
        exit(1);
      }
      *completeStrPtr = strdup(optarg);
      break;
    case 12:
      if (*startWithStrPtr) {
        free(*startWithStrPtr);
        printf("*** ***Only use the flag --startwithstr=<symbol> once.\n");
        exit(1);
      }
      *startWithStrPtr = strdup(optarg);
      break;

    case 9:
      if (def_or_undef == BOTH_DEF_AND_UNDEF) {
        def_or_undef = ONLY_DEF;
      } else {
        printf("*** ***Only use flag --def or --undef once.\n");
        exit(1);
      }
      break;
    case 10:
      if (def_or_undef == BOTH_DEF_AND_UNDEF) {
        def_or_undef = ONLY_UNDEF;
      } else {
        printf("*** ***Only use flag --def or --undef once.\n");
        exit(1);
      }
      break;

    case 'v':
      if (verbose != 0) {
        printf("*** ***Only use flag --verbose once.\n");
        exit(1);
      }
      verbose = 1;
      break;

    case '?':
      printf("\n*** invalid option given. (look above)\n\n");
      return -1;
      break;

    default:
      printf("?? getopt returned character code 0%o ??\n", c);
      return -1;
      break;
    }
  }

  if (optind < argc) {
    printf("non-option ARGV-elements: ");
    while (optind < argc)
      printf("%s ", argv[optind++]);
    printf("\n");
  }

  return 0;
}

void printObjectFileNames(int count, char *list[]) {
  if (debug_func)
    printf("printObjectFileNames\n");
  printf("The object files to modify are:\n");
  int i;
  for (i = 0; i < count; ++i)
    printf("(%d)\t%s\n", i, list[i]);
}

void printSymbolsToChange(int count, char *list[], char *str, FLAGTYPE ft) {
  if (debug_func)
    printf("printSymbolsToChange\n");
  switch (ft) {
  case SINGLESYM:
    printf("\n%s", "Single Symbols ");
    break;
  case KEEPNUMSYM:
    printf("\n%s", "Keep number Symbols ");
    break;
  case COMPLETESYM:
    printf("\n%s", "Complete Symbols ");
    break;
  case STARTWITHSYM:
    printf("\n%s", "Start With Symbols ");
    break;
  default:
    printf("*** ***Unknown flagtype in printSymbolsToChange function.");
    exit(1);
  }
  printf("planned on changing:\n");
  printf("%s:\n", str);
  int i;
  for (i = 0; i < count; ++i) {
    char buf[300] = {0};
    strcat(buf, list[i]);
    strcat(buf, " --> ");
    switch (ft) {
    case SINGLESYM:
      strcat(buf, list[i]);
      if (str) {
        strcat(buf, str);
      } else {
        strcat(buf, "__dmtcp_plt");
      }
      break;
    case KEEPNUMSYM:
      strcat(buf, list[i]);
      if (str) {
        strcat(buf, str);
        strcat(buf, "*");
      } else {
        strcat(buf, "__dmtcp_*");
      }
      break;
    case COMPLETESYM:
      strcat(buf, str);
      break;
    case STARTWITHSYM:
      strcat(buf, list[i]);
      strcat(buf, "<<<UNKNOWN>>>");
      strcat(buf, str);
      break;
    }
    printf("(%d)\t%s\n", i, buf);
  }
}

int checkAndFindElfFile(char *objFileName, Elf64_Ehdr *ehdr) {
  if (debug_func)
    printf("checkAndFindElfFile\n");

  read_metadata(objFileName, (char *)ehdr, sizeof(Elf64_Ehdr), 0);

  if (ehdr->e_ident[EI_MAG0] != 0x7f &&
      ehdr->e_ident[EI_MAG1] != 'E' && // CHECK IF ELF
      ehdr->e_ident[EI_MAG2] != 'L' && ehdr->e_ident[EI_MAG3] != 'F') {
    printf("Not an ELF executable\n");
    return -1;
  }
  switch (ehdr->e_type) {
  case ET_EXEC:
    printf("WARNING: The ELf type is that of an executable. (%s)\n",
           objFileName);
    break;
  case ET_REL:
    printf("SUCCESS: The ELF type is that of an relocatable. (%s)\n",
           objFileName);
    break;
  default:
    printf("ERROR: The ELF type is that NOT of an EXEC nor REL. (%s)\n",
           objFileName);
    return -1;
  }
  return 0;
}

int findSymtabAndStrtabAndShdr(char *objFileName, Elf64_Ehdr ehdr,
                               Elf64_Shdr **shdr, Elf64_Shdr **symtab,
                               Elf64_Shdr **strtab) {
  if (debug_func)
    printf("findSymtabAndStrtabAndShdr\n");
  unsigned long shoff = ehdr.e_shoff; // Section header table offset
  unsigned long shnum = ehdr.e_shnum; // Section header num entries
  *shdr = malloc(sizeof(Elf64_Shdr) *
                 ehdr.e_shnum); // Copy of section header table entries
  read_metadata(objFileName, (char *)*shdr, (shnum) * sizeof(Elf64_Shdr),
                shoff); // read in section headers

  Elf64_Shdr *hdr = *shdr;
  int idx;

  // Go through the section table entries
  for (idx = 0; idx < shnum; idx++, hdr++) {
    switch (hdr->sh_type) {
    case SHT_SYMTAB:
      *symtab = hdr;
      if (debug)
        printf("symtab: size=%lu offset=%p\n", hdr->sh_size,
               (void *)hdr->sh_offset);
      *strtab = *shdr + hdr->sh_link;
      if (debug)
        printf("strtab: size=%lu offset=%p\n", (*strtab)->sh_size,
               (void *)(*strtab)->sh_offset);
      assert((*strtab)->sh_type == SHT_STRTAB);
      break;
    }
  }
  if (*symtab == NULL || *strtab == NULL)
    return -1;
  if (debug)
    printf("%p %p %s\n", *symtab, *strtab,
           "Found Symbol table and String Table");
  return 0;
}

int loopSymbolTableForSymbol(int index, char **list, Elf64_Sym *symtab_ent,
                             unsigned int symtab_size, char *strtab_ent,
                             FLAGTYPE ft, int *symcountptr) {
  if (debug_func)
    printf("loopSymbolTableForSymbol\n");
  if (!index)
    return 0;
  Elf64_Sym *sym = NULL;

  if (ft == SINGLESYM)
    printf("\t%s\n", "Single Symbols");
  if (ft == KEEPNUMSYM)
    printf("\t%s\n", "Keep Number Symbols");
  if (ft == COMPLETESYM)
    printf("\t%s\n", "Complete Symbols");
  if (ft == STARTWITHSYM)
    printf("\t%s\n", "Start With Symbols");

  int i;
  for (i = 0; i < index; ++i) {
    int found = 0;
    for (sym = symtab_ent; (char *)sym < (char *)symtab_ent + symtab_size;
         sym++) {
      if (sym->st_name != 0) {
        char *symtab_symbol = strtab_ent + sym->st_name;

        if (strncmp(list[i], symtab_symbol, strlen(list[i])) == 0) {
          if ((def_or_undef == ONLY_DEF && sym->st_shndx == SHN_UNDEF) ||
              (def_or_undef == ONLY_UNDEF && sym->st_shndx != SHN_UNDEF)) {
            if (verbose)
              printf("\t\tContinue because of def or undef\n");
            continue;
          }

          printf("\t\tsymbol: %s  |  ", strtab_ent + sym->st_name);
          printf("sym->st_value: %p\n", (void *)sym->st_value);
          found = 1;

          switch (ft) {
          case STARTWITHSYM:
            actualStartWithSymbolsToReplace[actualStartWithSymbolsIndex++] =
                strdup(symtab_symbol);
            break;
          }
        }

        if (strcmp(symtab_symbol, list[i]) == 0) {
          if ((def_or_undef == ONLY_DEF && sym->st_shndx == SHN_UNDEF) ||
              (def_or_undef == ONLY_UNDEF && sym->st_shndx != SHN_UNDEF)) {
            if (verbose)
              printf("\t\tContinue because of def or undef\n");
            continue;
          }

          printf("\t\tsymbol: %s  |  ", strtab_ent + sym->st_name);
          printf("sym->st_value: %p\n", (void *)sym->st_value);
          found = 1;

          // Copy into correct (single | keepnum | complete) syms to replace
          // array
          switch (ft) {
          case SINGLESYM:
            actualSingleSymbolsToReplace[actualSingleSymbolsIndex++] =
                strdup(symtab_symbol);
            break;
          case KEEPNUMSYM:
            actualKeepNumSymbolsToReplace[actualKeepNumSymbolsIndex++] =
                strdup(symtab_symbol);
            break;
          case COMPLETESYM:
            actualCompleteSymbolsToReplace[actualCompleteSymbolsIndex++] =
                strdup(symtab_symbol);
            break;
          }
        }
      }
    }
    if (!found) {
      if (verbose)
        printf("\t\t*** ***Could not find: %s\n", list[i]);
      if (ft == KEEPNUMSYM) {
        printf("\t\tKeep Number Symbol : (%s) was NOT FOUND. [ERROR]\n",
               list[i]);
        return -1;
      }
    } else
      ++(*symcountptr);
  }
  if (verbose)
    printf("\t\tNumber of symbols to replace is: %d\n", *symcountptr);
  return 0;
}

int checkIfSymbolsExist(char *objFileName, int singleSymbolIndex,
                        char **singleSymbolList, int keepNumSymbolIndex,
                        char **keepNumSymbolList, int completeSymbolIndex,
                        char **completeSymbolList, int startWithSymbolIndex,
                        char **startWithSymbolList, Elf64_Shdr *symtab,
                        Elf64_Shdr *strtab, int *symcountptr) {
  if (debug_func)
    printf("checkIfSymbolsExit\n");
  Elf64_Sym *sym = NULL;
  Elf64_Sym *symtab_ent = NULL;
  char *strtab_ent = NULL;

  symtab_ent = malloc(symtab->sh_size);
  if (debug)
    printf("symtab: size=%lu offset=%p\n", symtab->sh_size,
           (void *)symtab->sh_offset);
  read_metadata(objFileName, (char *)symtab_ent, symtab->sh_size,
                symtab->sh_offset);
  strtab_ent = malloc(strtab->sh_size);
  if (debug)
    printf("strtab: size=%lu offset=%p\n", strtab->sh_size,
           (void *)strtab->sh_offset);
  read_metadata(objFileName, strtab_ent, strtab->sh_size, strtab->sh_offset);

  unsigned int symtab_size = symtab->sh_size;

  printf("In file %s symbols checked:\n", objFileName);

  // Loop for Single Sym
  loopSymbolTableForSymbol(singleSymbolIndex, singleSymbolList, symtab_ent,
                           symtab_size, strtab_ent, SINGLESYM, symcountptr);

  // Loop for Keep Num Sym
  if (loopSymbolTableForSymbol(keepNumSymbolIndex, keepNumSymbolList,
                               symtab_ent, symtab_size, strtab_ent, KEEPNUMSYM,
                               symcountptr) == -1) {
    return -1;
  }

  // Loop for Complete Sym
  loopSymbolTableForSymbol(completeSymbolIndex, completeSymbolList, symtab_ent,
                           symtab_size, strtab_ent, COMPLETESYM, symcountptr);

  // Loop for Start With Sym
  loopSymbolTableForSymbol(startWithSymbolIndex, startWithSymbolList,
                           symtab_ent, symtab_size, strtab_ent, STARTWITHSYM,
                           symcountptr);
  return 0;
}

int calculateBytesNeeded(int index, char **list, char *str, FLAGTYPE ft) {
  if (debug_func)
    printf("calculateBytesNeeded\n");
  int result = 0;

  int i;
  for (i = 0; i < index; ++i) {
    char strNum[300] = {0};
    sprintf(strNum, "%d", i + 1);

    switch (ft) {
    case SINGLESYM:
      result += strlen(list[i]);
      if (str)
        result += strlen(str) + 1;
      else
        result += strlen("__dmtcp_plt") + 1;
      break;
    case KEEPNUMSYM:
      result += strlen(list[i]);
      if (str)
        result += strlen(strNum) + 1;
      else
        result += strlen("__dmtcp_") + strlen(strNum) + 1;
      break;
    case COMPLETESYM:
      if (str)
        result += strlen(str) + 1;
      break;
    case STARTWITHSYM:
      result += strlen(list[i]);
      if (str)
        result += strlen(str) + 1;
      break;
    }
  }

  // align on 64 bytes..
  result = (result + 63) & -64;

  return result;
}

int extendAndFixAfterStrtab(char *objFileName, Elf64_Ehdr *ehdr,
                            Elf64_Shdr **shdr, Elf64_Shdr **symtab,
                            Elf64_Shdr **strtab, int add_space) {
  if (debug_func)
    printf("extendAndFixAfterStrtab\n");
  Elf64_Off begin_next_section = (*strtab)->sh_offset + (*strtab)->sh_size;
  Elf64_Off end_of_sections = (*strtab)->sh_offset + (*strtab)->sh_size;

  ehdr->e_shoff += add_space; // We will be displacing section header table
  (*strtab)->sh_size += add_space;

  if (debug)
    printf("bns:%p  eos:%p\n", (void *)begin_next_section,
           (void *)end_of_sections);

  // Modify Section Header Table and write back
  int idx = 0;
  Elf64_Shdr *ptr = *shdr;
  for (; idx < ehdr->e_shnum; ++idx, ++ptr) {
    Elf64_Shdr tmpshdr = *ptr;
    if (tmpshdr.sh_offset > (*strtab)->sh_offset) {
      ptr->sh_offset += add_space;
    }
    if (tmpshdr.sh_offset + tmpshdr.sh_size >
        end_of_sections) { // End of section header
      end_of_sections = tmpshdr.sh_offset + tmpshdr.sh_size;
    }
    if (debug)
      printf("bns:%p  eos:%p ptr:%p\n", (void *)begin_next_section,
             (void *)end_of_sections, ptr);
  }
  if (debug)
    printf("bns:%p  eos:%p\n", (void *)begin_next_section,
           (void *)end_of_sections);

  // Displace all sections after strtab by 'add_space'.
  assert(end_of_sections <= ehdr->e_shoff);
  if (end_of_sections > begin_next_section) {
    if (debug)
      printf("start of if: extendAndFixAfterStrtab\n");
    char tmpbuf[end_of_sections - begin_next_section];
    read_metadata(objFileName, tmpbuf, sizeof(tmpbuf), begin_next_section);
    // read_metadata(objFileName, tmpbuf, end_of_sections - begin_next_section,
    // begin_next_section);
    if (debug)
      printf("middle of if: between read_metadata and write_metadata "
             "extendAndFixAfterStrtab\n");
    write_metadata(objFileName, tmpbuf, sizeof(tmpbuf),
                   begin_next_section + add_space);
    if (debug)
      printf("end of if: extendAndFixAfterStrtab\n");
  }

  // Write back section headers and ELF header
  write_metadata(objFileName, (char *)*shdr,
                 (ehdr->e_shnum) * sizeof(Elf64_Shdr), ehdr->e_shoff);
  write_metadata(objFileName, (char *)ehdr, sizeof(Elf64_Ehdr), 0);

  return 0;
}

int addSymbolsAndUpdateSymtab(char *objFileName, int num, int index,
                              char **list, Elf64_Shdr *symtab,
                              Elf64_Shdr *strtab,
                              unsigned long *prev_strtab_size, char *str,
                              FLAGTYPE ft) {
  if (debug_func)
    printf("addSymbolsAndUpdateSymtab\n");
  if (index < 1)
    return 0;
  // From previous function checkIfSymbolsExist(), we know all the symbols exist

  int numSymbolsToChange = index;
  unsigned int symtab_size = symtab->sh_size;

  Elf64_Sym *sym = NULL;
  char symtab_buf[symtab->sh_size];
  char strtab_buf[strtab->sh_size];
  char numbuf[300] = {0};
  memset(numbuf, 0, sizeof(numbuf));
  sprintf(numbuf, "%d", num + 1);

  read_metadata(objFileName, symtab_buf, symtab->sh_size, symtab->sh_offset);
  read_metadata(objFileName, strtab_buf, strtab->sh_size, strtab->sh_offset);

  int i;
  for (i = 0; i < numSymbolsToChange; ++i) {
    for (sym = (void *)symtab_buf;
         (char *)sym < (char *)symtab_buf + symtab->sh_size; sym++) {
      if (sym->st_name != 0) {

        char *symtab_symbol = strtab_buf + sym->st_name;

        if (strcmp(symtab_symbol, list[i]) == 0) {
          int newStringSize = 0;
          char newString[300] = {0};

          memset(newString, 0, sizeof(newString));

          sym->st_name = *prev_strtab_size;

          switch (ft) {
          case SINGLESYM:
            strcat(newString, list[i]);
            if (str) {
              strcat(newString, str);
            } else {
              strcat(newString, "__dmtcp_plt");
            }
            break;
          case KEEPNUMSYM:
            strcat(newString, list[i]);
            if (str) {
              strcat(newString, str);
            } else {
              strcat(newString, "__dmtcp_");
            }
            strcat(newString, numbuf);
            break;
          case COMPLETESYM:
            strcat(newString, str);
            break;
          case STARTWITHSYM:
            strcat(newString, list[i]);
            strcat(newString, str);
            break;
          default:
            break;
          }
          strcat(newString, "\0");
          strncpy((char *)strtab_buf + *prev_strtab_size, newString,
                  strlen(newString) + 1);
          *prev_strtab_size += strlen(newString) + 1;

          if (debug)
            printf("NEW STRING IS **** %s ****\n", newString);
        }
      }
    }
    if (debug)
      printf("strtab->sh_size = %lu : prev = %lu\n", strtab->sh_size,
             *prev_strtab_size);
  }

  // Pad out with '\0'
  for (i = *prev_strtab_size; i < strtab->sh_size; ++i)
    strtab_buf[i] = '\0';

  write_metadata(objFileName, (char *)symtab_buf, symtab->sh_size,
                 symtab->sh_offset);
  write_metadata(objFileName, (char *)strtab_buf, strtab->sh_size,
                 strtab->sh_offset);

  return 0;
}

// Main
int main(int argc, char **argv) {
  if (debug_func)
    printf("main\n");
  int objIndex = 0;
  char **objList = malloc(3000 * sizeof(char *));

  int singleSymbolIndex = 0;
  char **singleSymbolList =
      malloc(300 * sizeof(char *)); // Symbols to add "__dmtcp_plt" to
  char *singleStr = NULL;

  int keepNumSymbolIndex = 0;
  char **keepNumSymbolList = malloc(
      300 * sizeof(char *)); // Symbols to add "__dmtcp_*" to, where * is 0 to n
  char *keepNumStr = NULL;

  int completeSymbolIndex = 0;
  char **completeSymbolList = malloc(
      300 * sizeof(char *)); // Symbols to compeltely replace with completeStr
  char *completeStr = NULL;

  int startWithSymbolIndex = 0;
  char **startWithSymbolList = malloc(
      300 * sizeof(char *)); // Symbols to add to that start with startWithStr
  char *startWithStr = NULL;

  printf(
      "\n\n%s\n\n",
      "+++++++++++++++++++++++++++++++++ Started replace-symbols-name Program");

  if (argc < 4) {
    // perror("Syntax: ./change-symbol-names <obj file> symbol [othersymbols]");
    perror("Syntax: ./replace-symbols-name [-o | -s | --singlesymbol | "
           "--keepnumsymbol]");
    exit(1);
  }

  // Parse the arguments!
  assert(runGetOpt(argc, argv, &objIndex, objList, &singleSymbolIndex,
                   singleSymbolList, &keepNumSymbolIndex, keepNumSymbolList,
                   &completeSymbolIndex, completeSymbolList,
                   &startWithSymbolIndex, startWithSymbolList, &singleStr,
                   &keepNumStr, &completeStr, &startWithStr) != -1);

  // Let user know which object files are going to change
  printObjectFileNames(objIndex, objList);

  // Let user know which symbols are going to change
  // assert(singleSymbolIndex + keepNumSymbolIndex > 0);
  if (singleSymbolIndex)
    printSymbolsToChange(singleSymbolIndex, singleSymbolList, singleStr,
                         SINGLESYM);
  if (keepNumSymbolIndex)
    printSymbolsToChange(keepNumSymbolIndex, keepNumSymbolList, keepNumStr,
                         KEEPNUMSYM);
  if (completeSymbolIndex)
    printSymbolsToChange(completeSymbolIndex, completeSymbolList, completeStr,
                         COMPLETESYM);
  if (startWithSymbolIndex)
    printSymbolsToChange(startWithSymbolIndex, startWithSymbolList,
                         startWithStr, STARTWITHSYM);
  printf("\n\n");

  int i;
  for (i = 0; i < objIndex; ++i) {
    Elf64_Shdr *shdr = NULL;
    Elf64_Shdr *symtab = NULL;
    Elf64_Shdr *strtab = NULL;
    unsigned long prev_strtab_size = 0;
    Elf64_Ehdr ehdr;

    memset(actualSingleSymbolsToReplace, 0,
           sizeof(actualSingleSymbolsToReplace));
    memset(actualKeepNumSymbolsToReplace, 0,
           sizeof(actualKeepNumSymbolsToReplace));
    memset(actualCompleteSymbolsToReplace, 0,
           sizeof(actualCompleteSymbolsToReplace));
    memset(actualStartWithSymbolsToReplace, 0,
           sizeof(actualStartWithSymbolsToReplace));
    actualSingleSymbolsIndex = 0;
    actualKeepNumSymbolsIndex = 0;
    actualCompleteSymbolsIndex = 0;
    actualStartWithSymbolsIndex = 0;

    // Check if file is valid and read in the ELF header
    assert(checkAndFindElfFile(objList[i], &ehdr) != -1);

    // Find symbol table and string table
    //   - shdr = malloc(); <-- on heap
    assert(findSymtabAndStrtabAndShdr(objList[i], ehdr, &shdr, &symtab,
                                      &strtab) != -1);
    prev_strtab_size = strtab->sh_size;

    // Check if the symbols exist first..
    int symcount = 0;
    assert(checkIfSymbolsExist(objList[i], singleSymbolIndex, singleSymbolList,
                               keepNumSymbolIndex, keepNumSymbolList,
                               completeSymbolIndex, completeSymbolList,
                               startWithSymbolIndex, startWithSymbolList,
                               symtab, strtab, &symcount) != -1);
    if (!symcount) {
      printf("        ^ ^ ^ continue\n\n");
      free(shdr);
      continue;
    }
    int i__;
    for (i__ = 0; i__ < actualStartWithSymbolsIndex; ++i__)
      printf("\t%s\n", actualStartWithSymbolsToReplace[i__]);
    // Extend the string table and whatever follows after..
    // Fix Elf header and Section header Table
    int add_space = calculateBytesNeeded(actualSingleSymbolsIndex,
                                         actualSingleSymbolsToReplace,
                                         singleStr, SINGLESYM) +
                    calculateBytesNeeded(actualKeepNumSymbolsIndex,
                                         actualKeepNumSymbolsToReplace,
                                         keepNumStr, KEEPNUMSYM) +
                    calculateBytesNeeded(actualCompleteSymbolsIndex,
                                         actualCompleteSymbolsToReplace,
                                         completeStr, COMPLETESYM) +
                    calculateBytesNeeded(actualStartWithSymbolsIndex,
                                         actualStartWithSymbolsToReplace,
                                         startWithStr, STARTWITHSYM);
    if (debug)
      printf("ADD_SPACE is: %x\n", add_space);
    assert(extendAndFixAfterStrtab(objList[i], &ehdr, &shdr, &symtab, &strtab,
                                   add_space) != -1);

    // Add dmtcp symbol name(s) and update symtab
    assert(addSymbolsAndUpdateSymtab(objList[i], i, actualSingleSymbolsIndex,
                                     actualSingleSymbolsToReplace, symtab,
                                     strtab, &prev_strtab_size, singleStr,
                                     SINGLESYM) != -1);
    assert(addSymbolsAndUpdateSymtab(objList[i], i, actualKeepNumSymbolsIndex,
                                     actualKeepNumSymbolsToReplace, symtab,
                                     strtab, &prev_strtab_size, keepNumStr,
                                     KEEPNUMSYM) != -1);
    assert(addSymbolsAndUpdateSymtab(objList[i], i, actualCompleteSymbolsIndex,
                                     actualCompleteSymbolsToReplace, symtab,
                                     strtab, &prev_strtab_size, completeStr,
                                     COMPLETESYM) != -1);
    assert(addSymbolsAndUpdateSymtab(objList[i], i, actualStartWithSymbolsIndex,
                                     actualStartWithSymbolsToReplace, symtab,
                                     strtab, &prev_strtab_size, startWithStr,
                                     STARTWITHSYM) != -1);

    int tmp_i;
    for (tmp_i = 0; tmp_i < actualSingleSymbolsIndex; ++tmp_i)
      free(actualSingleSymbolsToReplace[tmp_i]);
    for (tmp_i = 0; tmp_i < actualKeepNumSymbolsIndex; ++tmp_i)
      free(actualKeepNumSymbolsToReplace[tmp_i]);
    for (tmp_i = 0; tmp_i < actualCompleteSymbolsIndex; ++tmp_i)
      free(actualCompleteSymbolsToReplace[tmp_i]);
    for (tmp_i = 0; tmp_i < actualStartWithSymbolsIndex; ++tmp_i)
      free(actualStartWithSymbolsToReplace[tmp_i]);
    free(shdr);
  }
  free(singleSymbolList);
  free(keepNumSymbolList);
  free(completeSymbolList);
  free(startWithSymbolList);

  printf("\n\n%s\n\n", "Finished replace-symbols-name Program "
                       "+++++++++++++++++++++++++++++++++");
}
