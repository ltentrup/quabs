from subprocess import Popen, TimeoutExpired

from enum import Enum


#from analyze import FilePath


class SolverResult(Enum):
    SAT = 10
    UNSAT = 20
    UNKNOWN = 30
    CRASHED = -1
    TIMEOUT = -2

class SolverConfig(object):
    def __init__(self, name, executable, options=None):
        self.name = name
        self.executable = executable
        self.options = options if options is not None else []

    def getCommand(self):
        return [self.executable] + self.options

    def getBinary(self):
        return self.executable

    def run(self, instance, stdout, stderr, timeout=None):
        returncode = None
        cmd = self.getCommand()
        cmd.append(instance)
        try:
            proc = Popen(cmd, stdout=stdout, stderr=stderr)
            proc.wait(timeout=timeout)
        except TimeoutExpired:
            proc.kill()
            proc.wait()
            return SolverResult.TIMEOUT

        if proc.returncode in [SolverResult.SAT.value, SolverResult.UNSAT.value, SolverResult.UNKNOWN.value]:
            returncode = SolverResult(proc.returncode)
        else:
            returncode = SolverResult.CRASHED
        return returncode

    def __str__(self):
        return self.name
