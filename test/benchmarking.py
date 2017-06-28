#!/usr/bin/env python3

import argparse
import csv
import os
import re
import signal
import subprocess
import sys
import time
from tempfile import TemporaryFile

BASE_PATH = os.path.dirname(os.path.realpath(__file__))
SOLVER = os.path.join(BASE_PATH, '../caqe')

TIMEOUT = 120

SATISFIABLE = 10
UNSATISFIABLE = 20
UNKNOWN = 30

result_mapping = {
    SATISFIABLE: 'sat',
    UNSATISFIABLE: 'unsat'
}

TIME_UTIL = ['/usr/bin/time', '-v']
if sys.platform == 'darwin':
    # must be the GNU version of time (brew install gnu-time)
    TIME_UTIL = ['gtime', '-v']

def run(args):

    results = {} # file => run (result_code, time)/None

    csvwriter = csv.writer(sys.stdout)
    csvwriter.writerow(['File', 'Result', 'Time'])

    for f in os.listdir('./benchmarks/bounded/'):
        if not f.endswith('.qcir'):
            continue

        f = os.path.join('./benchmarks/bounded/', f)

        stdout = TemporaryFile()
        stderr = TemporaryFile()

        cmd = TIME_UTIL + [SOLVER, '--miniscoping', '--num-threads={}'.format(args.threads), f]

        try:
            proc = subprocess.Popen(cmd, stdout=stdout, stderr=stderr, preexec_fn=os.setsid)
            proc.wait(timeout=args.timeout)
            return_code = proc.returncode
        except subprocess.TimeoutExpired:
            #proc.kill()
            #os.kill(proc.pid, signal.SIGKILL)
            #os.waitpid(proc.pid, os.WNOHANG)
            os.killpg(proc.pid, signal.SIGUSR1)
            proc.wait()
            #subprocess.call('killall caqe', shell=True)

            results[f] = None
            csvwriter.writerow([f, 'timeout'])

            sys.stderr.write('T')
            sys.stderr.flush()
            time.sleep(1) # delays for 1 second
            continue

        stdout.seek(0)
        stderr.seek(0)

        #print(stdout.read())
        time_output = stderr.read().decode()
        elapsed = re.search(r"wall clock.*(\d+):(\d+.\d+)", time_output)
        minutes, seconds = elapsed.groups()
        minutes = int(minutes)
        seconds = float(seconds)
        time_elapsed = minutes*60 + seconds

        results[f] = (return_code, time_elapsed)
        if return_code not in result_mapping:
            csvwriter.writerow([f, 'failed'])
        else:
            csvwriter.writerow([f, result_mapping[return_code], time_elapsed])
        sys.stdout.flush()

        sys.stderr.write('.')
        sys.stderr.flush()
        time.sleep(1) # delays for 1 second


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--timeout', action='store', type=int, default=TIMEOUT,
                        help='timeout in seconds (default: {})'.format(TIMEOUT))
    parser.add_argument('--num-threads', dest='threads', action='store', type=int, default=1,
                        help='timeout in seconds (default: 1)')
    args = parser.parse_args()
    run(args)

if __name__ == "__main__":
    main()
