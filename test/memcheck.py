
import unittest
import subprocess
from tempfile import NamedTemporaryFile
import xml.etree.ElementTree as ET

from config import SOLVER_BINARY

class ValgrindMemcheck(unittest.TestCase):

    def __init__(self, test_name, test_file):
        super(ValgrindMemcheck, self).__init__('memcheck')
        self.test_name = test_name
        self.test_file = test_file

    def id(self):
        return "ValgrindMemcheck.{}".format(self.test_name)

    def memcheck(self):
        xml_out = NamedTemporaryFile()
        proc = subprocess.run(['valgrind', '--leak-check=yes', '--xml=yes', '--xml-file={}'.format(xml_out.name), SOLVER_BINARY, self.test_file], stdout=subprocess.DEVNULL)
        xml_out.seek(0)

        root = ET.fromstring(xml_out.read())
        errors = root.findall('error')
        if len(errors) > 0:
            self.fail('Memcheck found {} leaks'.format(len(errors)))
