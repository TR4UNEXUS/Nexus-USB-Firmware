#!/usr/bin/env python3
"""Exercise the production compressed reader with the bundled LZ4 implementation."""
from pathlib import Path
import os, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[2]
text=(ROOT/'libraries/libstratosphere/include/stratosphere/fssystem/fssystem_compressed_storage.hpp').read_text()
start=text.index('auto PerformRequiredRead ='); end=text.index('{',start)+1; depth=1
while depth:
    depth+=(text[end]=='{')-(text[end]=='}'); end+=1
body=text[start:end]+';'
fixture=Path(__file__).with_name('compressed_read_fixture.cpp.in').read_text()
assert fixture.count('@PRODUCTION_READ_LAMBDA@')==1
lz4=ROOT/'libraries/libstratosphere/source/util'
with tempfile.TemporaryDirectory(prefix='nexus-compressed-') as directory:
    out=Path(directory); source=out/'test.cpp'; source.write_text(fixture.replace('@PRODUCTION_READ_LAMBDA@',body))
    subprocess.run(['gcc','-O2','-c',str(lz4/'lz4.c'),'-o',str(out/'lz4.o')],check=True)
    for opt in ('-O0','-O2'):
        exe=out/('test'+opt)
        subprocess.run(['g++','-std=c++20',opt,'-g','-UNDEBUG','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-no-pie','-I',str(lz4),str(source),str(out/'lz4.o'),'-o',str(exe)],check=True)
        result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30,env={**os.environ,'ASAN_OPTIONS':'handle_segv=0:abort_on_error=1'})
        assert result.returncode==0 and 'cases=31 failed=0' in result.stdout,result.stdout+result.stderr
        assert 'AddressSanitizer' not in result.stderr and 'runtime error:' not in result.stderr,result.stderr
        print(opt+': cases=31 failed=0; ASan/UBSan passed')
