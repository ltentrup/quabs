#!/usr/bin/env python3

""" Testing script for external tool that is easily extendable. """

import argparse
import os

from analyze import TestManager
from dummy import DummyTestMethod
from execute import ExecutionTestMethod
from memcheck_new import MemcheckMethod
from certification import CertificationTestMethod
from solver_config import SolverConfig

TIMEOUT = 10

script_dir = os.path.dirname(os.path.realpath(__file__))

def get_test_cases(directory):
    test_cases = []
    abs_dir = os.path.join(script_dir, directory)
    for f in os.listdir(abs_dir):
        if f.endswith('.qcir'):
            test_cases.append(os.path.join(abs_dir, f))
    return test_cases

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--timeout', action='store', type=int, default=TIMEOUT,
                        help='timeout in seconds (default: {})'.format(TIMEOUT))
    parser.add_argument('--xml', action='store_const', default=None, const=True,
                        help='write results to JUnit compatible XML file')

    # Test cases
    parser.add_argument('--unittests', action='store_const', default=None, const="./unittests/",
                        help='add unittest instances')
    parser.add_argument('--fuzztests', action='store_const', default=None, const="./fuzzed/",
                        help='add fuzzed instances')
    parser.add_argument('--examples', action='store_const', default=None, const="./examples/",
                        help='add example instances')

    # Test methods
    parser.add_argument('--execution', action='store_const', default=None, const=ExecutionTestMethod(),
                        help='add execution test method')
    parser.add_argument('--memcheck', action='store_const', default=None, const=MemcheckMethod(),
                        help='add memcheck test')
    parser.add_argument('--certification', action='store_const', default=None, const=CertificationTestMethod(),
                        help='add certification test')
                        
    parser.add_argument('binary', action='store')

    args = parser.parse_args()

    solver_configs = [
        SolverConfig("quabs", args.binary, ['--statistics']),
        SolverConfig("quabs", args.binary, ['--statistics']),
    ]

    test_cases = []

    if args.unittests:
        test_cases.extend(get_test_cases(args.unittests))
    if args.fuzztests:
        test_cases.extend(get_test_cases(args.fuzztests))
    if args.examples:
        test_cases.extend(get_test_cases(args.examples))

    test_methods = []

    if args.execution:
        test_methods.append(args.execution)
    if args.memcheck:
        test_methods.append(args.memcheck)
    if args.certification:
        test_methods.append(args.certification)

    manager = TestManager(test_cases, solver_configs, test_methods, args.timeout)
    failed = manager.run()
    if failed:
        exit(1)

if __name__ == "__main__":
    main()
