import os
import re
from subprocess import DEVNULL, PIPE, Popen
from tempfile import TemporaryFile

from analyze import (FilePath, Result, TestMethod, TestResult,
                     TestResultAggregator)
from solver_config import SolverConfig, SolverResult

script_dir = os.path.dirname(os.path.realpath(__file__))
CERTCHECK = os.path.join(script_dir, '../certcheck')
AIGTOCNF = 'aigtocnf'
SATSOLVER = 'picosat'

class CertificationTestResult(TestResult):

    def __init__(self, instance: FilePath, solver: SolverConfig):
        super().__init__(instance, "CertificationTestMethod", solver)

        self.iterations = None

        # Extract information about result from instance
        self.expected = SolverResult.UNKNOWN
        for line in open(instance, 'r'):
            if 'r SAT' in line:
                self.expected = SolverResult.SAT
            elif 'r UNSAT' in line:
                self.expected = SolverResult.UNSAT

    def setCertificationResult(self, satsolver_result, output=None):
        if satsolver_result == 20:
            self.result = Result.succeeded
        elif satsolver_result == 10:
            self.result = Result.failed
            self.reason = "found a counterexample to certificate"
            self.output = output
        else:
            self.result = Result.failed
            self.reason = "error in certification toolchain"
            self.output = output

    def __str__(self):
        string = super().__str__()
        return string

class CertificationTestResultAggregator(TestResultAggregator):

    def __init__(self):
        super().__init__()

    def addTestResult(self, result: CertificationTestResult):
        super().addTestResult(result)

    def getAnalysis(self):
        analysis = super().getAnalysis()
        return analysis

class CertificationTestMethod(TestMethod):

    def __init__(self):
        super().__init__(name="CertificationTestMethod")

    def run(self, instance: FilePath, solver: SolverConfig, timeout=None) -> TestResult:
        result = CertificationTestResult(instance, solver)

        stderr_mapping = TemporaryFile()
        cmd = [solver.getBinary(), '-c', instance]  # with certification flag

        solver = Popen(cmd, stdout=PIPE, stderr=stderr_mapping)
        check = Popen([CERTCHECK, instance], stdin=solver.stdout, stdout=PIPE, stderr=stderr_mapping)
        cnf_conv = Popen([AIGTOCNF], stdin=check.stdout, stdout=PIPE, stderr=stderr_mapping)
        satsolving = Popen([SATSOLVER], stdin=cnf_conv.stdout, stdout=stderr_mapping, stderr=stderr_mapping)
        satsolving.wait()

        stderr_mapping.seek(0)
        output = stderr_mapping.read().decode()

        result.setCertificationResult(satsolving.returncode, output.strip())
        return result

    def getAnalyzer(self) -> CertificationTestResultAggregator:
        return CertificationTestResultAggregator()
