import os
import subprocess
import xml.etree.ElementTree as ET
from tempfile import NamedTemporaryFile

from analyze import (FilePath, Result, TestMethod, TestResult,
                     TestResultAggregator)
from solver_config import SolverConfig

script_dir = os.path.dirname(os.path.realpath(__file__))

class Frame(object):
    def __init__(self, ip, fn, file, line):
        self.ip = ip
        self.fn = fn
        self.file = file
        self.line = line

    def __eq__(self, other):
        return self.ip == other.ip

    def __str__(self):
        return "file {}, function {}, line {}".format(self.file, self.fn, self.line)

class StackFrame(object):
    def __init__(self):
        self.frame = []

    def addFrame(self, frame: Frame):
        self.frame.append(frame)

    def __eq__(self, other):
        if len(self.frame) != len(other.frame):
            return False
        for frame1, frame2 in zip(self.frame, other.frame):
            if frame1 != frame2:
                return False
        return True

    def __str__(self):
        return '\n - '.join([str(f) for f in self.frame])

class MemcheckResult(TestResult):

    def __init__(self, instance: FilePath, solver: SolverConfig):
        super().__init__(instance, "Memcheck", solver)
        self.leaks = []

    def set(self, leaks, output=None):
        self.result = Result.succeeded
        for leak in leaks:
            self.parseLeak(leak)

    def parseLeak(self, leak):
        # search the stack frame until first occurrence of solver
        stack = StackFrame()
        empty = True
        for frame in leak.iter("frame"):
            obj = frame.find('obj').text
            if obj != self.solver.executable:
                continue

            ip = frame.find('ip').text
            fn = frame.find('fn').text
            file = frame.find('file').text
            line = frame.find('line').text
            stack.addFrame(Frame(ip, fn, file, line))
            empty = False

        if not empty:
            self.leaks.append(stack)
            self.result = Result.failed

    def __str__(self):
        string = super().__str__()
        if self.leaks:
            string += "\nLeaks: {}".format(len(self.leaks))
        for leak in self.leaks:
            string += "\n{}".format(leak)
        return string

class MemcheckResultAggregator(TestResultAggregator):

    def __init__(self):
        super().__init__()
        self.leaks = []

    def addTestResult(self, result: MemcheckResult):
        super().addTestResult(result)
        for leak in result.leaks:
            if leak not in self.leaks:
                self.leaks.append(leak)

    def getAnalysis(self):
        analysis = super().getAnalysis()
        return "{}, {} unique leaks".format(analysis, len(self.leaks))

class MemcheckMethod(TestMethod):

    def __init__(self):
        super().__init__(name="Memcheck")

    def run(self, instance: FilePath, solver: SolverConfig, timeout=None) -> TestResult:
        result = MemcheckResult(instance, solver)
        xml_out = NamedTemporaryFile()
        proc = subprocess.run([
            'valgrind',
            '--leak-check=yes',
            '--suppressions={}'.format(os.path.join(script_dir, 'osx.supp')),
            '--xml=yes',
            '--xml-file={}'.format(xml_out.name)
        ] + solver.getCommand() + [instance], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        xml_out.seek(0)

        root = ET.fromstring(xml_out.read())
        errors = root.findall('error')
        result.set(errors)

        return result

    def getAnalyzer(self) -> MemcheckResultAggregator:
        return MemcheckResultAggregator()
