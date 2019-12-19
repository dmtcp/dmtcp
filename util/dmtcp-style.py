#!/usr/bin/env python

''' Runs checks for DMTCP style. '''

import os
import re
import string
import subprocess
import sys


class LinterBase(object):
    '''
    This is an abstract class that provides the base functionality for
    linting files in the DMTCP project. Its 'main()' function
    walks through the set of files passed to it and runs some
    standard linting over them. This includes checking for license headers
    and checking for non-supported characters. From there it calls a
    'run_lint()' function that can be overridden to provide
    customizable style checks for a specific class of files (e.g. C++,
    Python, etc.).

    Any class that extends from 'LinterBase' should override the
    following class variables / functions:

    linter_type
    source_dirs
    exclude_files
    source_files
    comment_prefix

    run_lint()

    Please see the comments below for details on how to override each
    variable.
    '''
    # The name of the linter to help with printing which linter files
    # are currently being processed by.
    linter_type = ''

    # Root source paths (will be traversed recursively).
    source_dirs = []

    # Add file paths and patterns which should not be checked
    # This should include 3rdparty libraries, includes and machine generated
    # source.
    exclude_files = ''

    # A regex of possible matches for your source files.
    source_files = ''

    # A prefix at the beginning of the line to demark comments (e.g. '//')
    comment_prefix = ''

    def find_candidates(self, root_dir):
        '''
        Search through the all files rooted at 'root_dir' and compare
        them against 'self.exclude_files' and 'self.source_files' to
        come up with a set of candidate files to lint.
        '''
        exclude_file_regex = re.compile(self.exclude_files)
        source_criteria_regex = re.compile(self.source_files)
        for root, dirs, files in os.walk(root_dir):
            for name in files:
                path = os.path.join(root, name)
                if exclude_file_regex.search(path) is not None:
                    continue

                if source_criteria_regex.search(name) is not None:
                    yield path

    def run_lint(self, source_paths):
        '''
        A custom function to provide linting for 'linter_type'.
        It takes a list of source files to lint and returns the number
        of errors found during the linting process.

        It should print any errors as it encounters them to provide
        feedback to the caller.
        '''
        pass

    def main(self, file_list):
        '''
        This function takes a list of files and lints them for the
        class of files defined by 'linter_type'.
        '''

        # Verify that source roots are accessible from current working directory.
        # A common error could be to call the style checker from other
        # (possibly nested) paths.
        for source_dir in self.source_dirs:
            if not os.path.exists(source_dir):
                print("Could not find '{dir}'".format(dir=source_dir))
                print('Please run from the root of the DMTCP source directory')
                exit(1)

        # Add all source file candidates to candidates list.
        candidates = []
        for source_dir in self.source_dirs:
            for candidate in self.find_candidates(source_dir):
                candidates.append(candidate)

        # If file paths are specified, check all file paths that are
        # candidates; else check all candidates.
        file_paths = file_list if len(file_list) > 0 else candidates

        # Compute the set intersect of the input file paths and candidates.
        # This represents the reduced set of candidates to run lint on.
        candidates_set = set(candidates)
        clean_file_paths_set = set(map(lambda x: x.rstrip(), file_paths))
        filtered_candidates_set = clean_file_paths_set.intersection(
            candidates_set)

        if filtered_candidates_set:
            plural = '' if len(filtered_candidates_set) == 1 else 's'
            print('Checking {num_files} {linter} file{plural}'.
                    format(num_files=len(filtered_candidates_set),
                           linter=self.linter_type,
                           plural=plural))

            total_errors = self.run_lint(list(filtered_candidates_set))

            sys.stderr.write('Total errors found: {num_errors}\n'.\
                                format(num_errors=total_errors))

            return total_errors
        else:
            print("No {linter} files to lint".format(linter=self.linter_type))
            return 0


class CppLinter(LinterBase):
    linter_type = 'C++'

    source_dirs = ['src',
                   'include',
                   'plugin',
                   'contrib']

    exclude_files = '(restartscript.cpp|infiniband\/examples|src\/mtcp\/|test-suite|Makefile.in)'

    source_files = '\.(cpp|h)$'

    comment_prefix = '\/\/'

    def run_lint(self, source_paths):
        '''
        Runs cpplint over given files.

        http://google-styleguide.googlecode.com/svn/trunk/cpplint/cpplint.py
        '''

        # See cpplint.py for full list of rules.
        active_rules = [
            'build/class',
            'build/deprecated',
            'build/endif_comment',
            'build/include_order',
            #'build/include_what_you_use',
            #'readability/todo',
            #'readability/namespace',
            #'runtime/vlog',
            'whitespace/blank_line',
            'whitespace/comma',
            'whitespace/end_of_line',
            'whitespace/ending_newline',
            'whitespace/forcolon',
            'whitespace/indent',
            'whitespace/line_length',
            'whitespace/operators',
            'whitespace/semicolon',
            'whitespace/tab',
            'whitespace/comments',
            'whitespace/todo']

        rules_filter = '--filter=-,+' + ',+'.join(active_rules)
        p = subprocess.Popen(
            ['python', 'util/cpplint.py', rules_filter] + source_paths,
            stderr=subprocess.PIPE,
            close_fds=True)

        # Lines are stored and filtered, only showing found errors instead
        # of e.g., 'Done processing XXX.' which tends to be dominant output.
        for line in p.stderr:
            if re.match('^(Done processing |Total errors found: )', line):
                continue
            sys.stderr.write(line)

        p.wait()
        return p.returncode


if __name__ == '__main__':
    cpp_linter = CppLinter()
    cpp_errors = cpp_linter.main(sys.argv[1:])
    sys.exit(cpp_errors)
