import sys

from enum import Enum

from solver_config import SolverConfig
from typing import Sequence

# Type alias
FilePath = str

# Bash highlight
def bash_bold(string:str) -> str:
    return '\033[1m' + string + '\033[0m'

class Result(Enum):
    succeeded = 0
    failed    = 1
    skipped   = 2

    def short_name(self):
        return [".", "F", "S"][self.value]

class TestResult(object):
    """ Defines the interface for test results """

    def __init__(self, instance: FilePath, test_method: str, solver: SolverConfig):
        self.instance = instance
        self.test_method = test_method
        self.solver = solver
        self.result = Result.skipped
        self.reason = None
        self.output = None

    def get(self) -> Result:
        return self.result

    def timeout(self):
        self.result = Result.skipped
        self.reason = "Timeout expired"

    def fail(self, reason: str):
        self.result = Result.failed
        self.reason = reason

    def __str__(self):
        res = "{} ({} {}) : {}".format(self.instance, self.solver, self.test_method, self.result.name)
        if self.reason is not None:
            res += "\nReason: " + self.reason
        if self.output is not None:
            res += "\n\nSolver output:\n" + self.output
        return res

class TestResultAggregator(object):

    def __init__(self):
        self.num_tests = 0
        self.num_succeeded = 0
        self.num_failed = 0
        self.num_skipped = 0

    def addTestResult(self, result: TestResult):
        self.num_tests += 1
        if result.get() == Result.succeeded:
            self.num_succeeded += 1
            return
        elif result.get() == Result.failed:
            self.num_failed += 1
        else:
            self.num_skipped += 1

        print("\n{0}\n{1}\n{0}\n".format("-" * 80, result))

    def getAnalysis(self):
        assert self.num_tests == (self.num_succeeded + self.num_failed + self.num_skipped)
        return "{}/{}/{}".format(self.num_succeeded, self.num_failed, self.num_skipped)

class TestMethod(object):
    """ Defines the interface for test methods """

    def __init__(self, name: str):
        self.name = name

    def run(self, instance: FilePath, timeout=None) -> TestResult:
        return None

    def getAnalyzer(self) -> TestResultAggregator:
        return TestResultAggregator()

class TestManager(object):
    """ Execution of test methods, capturing of results """

    def __init__(self, instances: Sequence[FilePath], solver_configs: Sequence[SolverConfig], methods: Sequence[TestMethod], timeout=None):
        self.instances = instances
        self.solver_configs = solver_configs
        self.methods = methods
        self.timeout = timeout

        self.results = {} # solver => result
        self.analysis = {} # solver => result analyzer

    def run(self):
        failed = False
        for solver in self.solver_configs:
            self.results[solver] = {}
            self.analysis[solver] = {}
            sys.stdout.write('\n{}\n'.format(bash_bold(solver.name)))
            for method in self.methods:
                sys.stdout.write('\n{}\n'.format(method.name))
                self.analysis[solver][method] = method.getAnalyzer()
                self.results[solver][method] = []
                for instance in self.instances:
                    result = method.run(instance, solver, self.timeout)
                    self.results[solver][method].append(result)
                    sys.stdout.write(result.get().short_name())
                    sys.stdout.flush()
                sys.stdout.write("\n")
            sys.stdout.write("\n")

        for solver in self.results:
            for method in self.results[solver]:
                for result in self.results[solver][method]:
                    self.analysis[solver][method].addTestResult(result)

        for solver in self.solver_configs:
            print(solver)
            for method in self.methods:
                print("  {} {}".format(method.name, self.analysis[solver][method].getAnalysis()))
                if self.analysis[solver][method].num_failed > 0:
                    failed = True
        
        return failed
