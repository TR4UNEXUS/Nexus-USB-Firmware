#!/usr/bin/env python3
"""Test production overlay callbacks and firmware eject gates on a host."""
from pathlib import Path
import subprocess, tempfile
tests=Path(__file__).resolve().parent
project=tests.parent
root=project.parents[1]
with tempfile.TemporaryDirectory(prefix='nexus-overlay-') as temp:
    work=Path(temp)
    for name in ('ui','eject'):
        exe=work/('test-'+name)
        subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror',
            '-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-pie','-no-pie',
            '-I'+str(tests/'stubs'),'-I'+str(project/'include'),'-I'+str(project/'source'),
            '-I'+str(root/'libraries/libstratosphere/include/stratosphere/usb'),
            str(tests/('test_'+name+'.cpp')),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],cwd=work,check=True,timeout=30)
