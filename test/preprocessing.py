
import unittest
import subprocess
from tempfile import NamedTemporaryFile

from config import SOLVER_BINARY, QCIR2QCIR_BINARY, QCIR2QDIMACS_BINARY

qdimacs_solver = 'depqbf'

class PreprocessingTest(unittest.TestCase):

    def __init__(self, test_name, test_file):
        super(PreprocessingTest, self).__init__('test_preprocessing')
        self.test_name = test_name
        self.test_file = test_file

    def id(self):
        return "PreprocessingTest.{}".format(self.test_name)

    def test_preprocessing(self):
        qdimacs_before = NamedTemporaryFile()
        qcir_after = NamedTemporaryFile()
        qdimacs_after = NamedTemporaryFile()
        
        subprocess.run([QCIR2QDIMACS_BINARY, self.test_file], stdout=qdimacs_before)
        subprocess.run([QCIR2QCIR_BINARY, '--preprocess', self.test_file], stdout=qcir_after)
        subprocess.run([QCIR2QDIMACS_BINARY, qcir_after.name], stdout=qdimacs_after)
        
        before = subprocess.run([qdimacs_solver, qdimacs_before.name], stdout=subprocess.DEVNULL)
        after = subprocess.run([qdimacs_solver, qdimacs_after.name], stdout=subprocess.DEVNULL)
        
        if before.returncode != after.returncode:
            self.fail("Different return codes of solver before ({}) and after ({}) preprocessing".format(before.returncode, after.returncode))
