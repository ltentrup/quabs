import re
from tempfile import TemporaryFile

from analyze import (FilePath, Result, TestMethod, TestResult,
                     TestResultAggregator)
from solver_config import SolverConfig, SolverResult


class ExecutionTestResult(TestResult):

    def __init__(self, instance: FilePath, solver: SolverConfig):
        super().__init__(instance, "ExecutionTestMethod", solver)

        self.iterations = None

        # Extract information about result from instance
        self.expected = SolverResult.UNKNOWN
        for line in open(instance, 'r'):
            if 'r SAT' in line:
                self.expected = SolverResult.SAT
            elif 'r UNSAT' in line:
                self.expected = SolverResult.UNSAT

    def setSolverResult(self, solver_result: SolverResult, output=None):
        self.solver_result = solver_result
        if self.solver_result == SolverResult.UNKNOWN:
            self.result = Result.skipped
            self.reason = "Instance was not solved"
        elif self.expected == SolverResult.UNKNOWN:
            self.result = Result.skipped
            self.reason = "Expected result could not be determined"
        elif solver_result == SolverResult.UNKNOWN:
            self.result = Result.skipped
            self.reason = "Solver has not solved instance"
        elif solver_result != self.expected:
            self.reason = "Expected {} but was {}".format(self.expected.name, solver_result.name)
            self.output = output
            self.result = Result.failed
        else:
            self.result = Result.succeeded

    def setIterations(self, iterations):
        self.iterations = iterations

    def __str__(self):
        string = super().__str__()
        if self.iterations is not None:
            string += "\nIterations: {}".format(self.iterations)
        return string

class ExecutionTestResultAggregator(TestResultAggregator):

    def __init__(self):
        super().__init__()
        self.iterations = 0

    def addTestResult(self, result: ExecutionTestResult):
        super().addTestResult(result)
        if result.iterations is not None:
            self.iterations += result.iterations

    def getAnalysis(self):
        analysis = super().getAnalysis()
        return "{} {} iterations".format(analysis, self.iterations)

class ExecutionTestMethod(TestMethod):

    def __init__(self):
        super().__init__(name="ExecutionTestMethod")

    def run(self, instance: FilePath, solver: SolverConfig, timeout=None) -> TestResult:
        result = ExecutionTestResult(instance, solver)

        stdout_mapping = TemporaryFile()
        stderr_mapping = TemporaryFile()
        solver_result = solver.run(instance, stdout_mapping, stderr_mapping, timeout)
        if solver_result == SolverResult.TIMEOUT:
            result.timeout()
            return result
        stdout_mapping.seek(0)
        stderr_mapping.seek(0)
        output = stdout_mapping.read().decode() + stderr_mapping.read().decode()

        result.setSolverResult(solver_result, output.strip())

        # Get iterations from solver output
        iterations = 0
        for match in re.finditer(r"Count\s*:\s*(\d+)", output):
            iterations += int(match.group(1))
        if iterations > 0:
            result.setIterations(iterations)
        return result

    def getAnalyzer(self) -> ExecutionTestResultAggregator:
        return ExecutionTestResultAggregator()
