#!/usr/bin/env python3

''' Runs caqe on all instances in the directory of this script '''

import argparse
import os
import unittest
from subprocess import Popen, TimeoutExpired
from tempfile import TemporaryFile

from memcheck import ValgrindMemcheck
from preprocessing import PreprocessingTest

try:
    import xmlrunner
except ImportError:
    print('Cannot export test results as JUnit XML, install `unittest-xml-reporting`')

script_dir = os.path.dirname(os.path.realpath(__file__))
solver_binary = os.path.join(script_dir, '../caqe')

TIMEOUT = 10

RESULT_SAT = 10
RESULT_UNSAT = 20
RESULT_UNKNOWN = 30
RESULT_CRASHED = 1
RESULT_TIMEOUT = 2
RESULT_WRONG = 3

result_mapping = {
    RESULT_SAT:     'success',
    RESULT_UNSAT:   'success',
    RESULT_UNKNOWN: 'unknown',
    RESULT_CRASHED: 'crashed',
    RESULT_TIMEOUT: 'timeout',
    RESULT_WRONG:   'wrong'
}

def run_caqe(qcir_file, stdout, stderr, options=None):
    returncode = None
    cmd = [solver_binary]
    if options:
        cmd.extend(options)
    cmd.append(qcir_file)
    proc = Popen(cmd, stdout=stdout, stderr=stderr)
    try:
        proc.wait(timeout=TIMEOUT)
        if proc.returncode in [RESULT_SAT, RESULT_UNSAT, RESULT_UNKNOWN]:
            returncode = proc.returncode
        else:
            returncode = RESULT_CRASHED
    except TimeoutExpired:
        proc.kill()
        proc.wait()
        returncode = RESULT_TIMEOUT
    return returncode

class CaqeRunTestCase(unittest.TestCase):

    def __init__(self, testmethod, test_name, qcir_file, options=None):
        super(CaqeRunTestCase, self).__init__(testmethod)
        self.qcir_file = qcir_file
        self.name = test_name
        self.expected = None
        self.stdout = TemporaryFile()
        self.stderr = TemporaryFile()
        self.options = options

        for line in open(qcir_file, 'r'):
            if 'r SAT' in line:
                self.expected = RESULT_SAT
            elif 'r UNSAT' in line:
                self.expected = RESULT_UNSAT

    def id(self):
        if not self.options:
            return "CaqeRunTestCase.{}".format(self.name)
        else:
            return "CaqeRunTestCase.{} {}".format(self.name, ' '.join(self.options))

    def test_run(self):
        returncode = run_caqe(self.qcir_file, self.stdout, self.stderr, options=self.options)
        
        self.stdout.seek(0)
        self.stderr.seek(0)
        self.evaluate_results(returncode, self.stdout.read().decode() + self.stderr.read().decode())

    def evaluate_results(self, returncode, output):
        result = returncode
        if returncode in [RESULT_SAT, RESULT_UNSAT] and returncode != self.expected:
            if self.expected is None:
                result = RESULT_UNKNOWN
            else:
                result = RESULT_WRONG

        if returncode == RESULT_UNKNOWN:
            self.skipTest(reason=result_mapping[returncode])
        elif result in [RESULT_UNKNOWN, RESULT_TIMEOUT]:
            self.skipTest(reason=result_mapping[result])
        elif result in [RESULT_WRONG, RESULT_CRASHED]:
            self.fail(result_mapping[result] + '\n\nOutput:' + output)

        self.assertIn(result, [RESULT_SAT, RESULT_UNSAT])


class CaqeCheckOutput(unittest.TestCase):
    ''' Check that there is no unnecessary output '''

    def test_output(self):
        stdout = TemporaryFile()
        stderr = TemporaryFile()
        run_caqe(os.path.join(script_dir, 'unittests/bosy_sat.qcir'), stdout, stderr)
        stdout.seek(0)
        for line in stdout:
            line = line.decode().strip()
            if not line:
                continue
            elif 'c caqe' in line:
                continue
            elif 'r SAT' in line:
                continue
            elif 'r UNSAT' in line:
                continue
            else:
                self.fail('Found unnecessary output {}'.format(line.strip()))

        stderr.seek(0)
        self.assertFalse(stderr.read().strip())

def main():
    global TIMEOUT

    parser = argparse.ArgumentParser()
    parser.add_argument('--timeout', dest='timeout', action='store', type=int, default=TIMEOUT,
                        help='Timeout in seconds (default: {})'.format(TIMEOUT))
    parser.add_argument('--xml', dest='xml', action='store_const', default=None, const=True,
                        help='Write results to JUnit compatible XML file')
    parser.add_argument('--full', dest='full', action='store_const', default=None, const=True,
                        help='Run all configurations of CAQE')
    parser.add_argument('--fuzzing', action='store_const', default=None, const=True,
                        help='Run on fuzzed instances')
    parser.add_argument('--memcheck', action='store_const', default=None, const=True,
                        help="Run valgrind's memcheck tool")
    parser.add_argument('--preprocessing', action='store_const', default=None, const=True,
                        help="Run preprocessing tests")
    args = parser.parse_args()

    TIMEOUT = args.timeout

    if args.fuzzing:
        unittest_dir = os.path.join(script_dir, 'fuzzing')
    else:
        unittest_dir = os.path.join(script_dir, 'unittests')

    # Collect test files
    test_files = []
    for f in os.listdir(unittest_dir):
        if f.endswith('.qcir'):
            test_files.append(os.path.join(unittest_dir, f))

    # Determine command line options
    options = [None]
    if args.full:
        options.append(['--disable-preprocessing'])

    suite = unittest.TestSuite()
    for qcir_file in test_files:
        for option in options:
            test_name = os.path.basename(qcir_file).replace('.qcir', '')
            suite.addTest(CaqeRunTestCase('test_run', test_name, qcir_file, option))
            if args.memcheck:
                suite.addTest(ValgrindMemcheck(test_name, qcir_file))
            if args.preprocessing:
                suite.addTest(PreprocessingTest(test_name, qcir_file))

    suite.addTest(CaqeCheckOutput('test_output'))

    testrunner = unittest.TextTestRunner()
    if args.xml:
        testrunner = xmlrunner.XMLTestRunner(output=os.path.join(script_dir, 'reports'), outsuffix='')  # overwrite older files

    if not testrunner.run(suite).wasSuccessful():
        exit(1)

if __name__ == "__main__":
    main()
