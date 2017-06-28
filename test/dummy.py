from analyze import TestMethod, TestResult, FilePath, Result
from solver_config import SolverConfig

class DummyTestResult(TestResult):
    def get(self):
        return Result.succeeded

class DummyTestMethod(TestMethod):

    def __init__(self):
        super().__init__(name="DummyTestMethod")

    def run(self, instance: FilePath, solver: SolverConfig, timeout=None) -> TestResult:
        return DummyTestResult()
