#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile the verbatim production EndpointSession::PostBuffer with fake USB IPC/events."""
from pathlib import Path
import re,shutil,subprocess,tempfile
root=Path(__file__).resolve().parents[2];lib=root/'libraries/libstratosphere';tests=Path(__file__).resolve().parent
source=(lib/'source/usb/usb_host.cpp').read_text()
start=source.index('    Result EndpointSession::PostBuffer(')
end=source.index('\n    }',start)+len('\n    }')
method=source[start:end]
timeout=re.search(r'        constexpr u64 EndpointTransferTimeoutNs = .*;',source).group()
with tempfile.TemporaryDirectory(prefix='nexus-completion-probe-') as tmp:
    stage=Path(tmp);(stage/'actual_completion_post_buffer.hpp').write_text(timeout+'\n'+method+'\n')
    exe=stage/'probe-tests'
    subprocess.run([shutil.which('g++'),'-std=c++20','-O2','-fsanitize=undefined','-fno-sanitize-recover=all','-Wall','-Wextra','-Werror','-pthread',
        '-I'+str(stage),'-I'+str(tests/'sector_cache_host'),'-I'+str(tests/'writable_volume_host'),'-I'+str(lib/'include'),
        str(tests/'completion_probe_host_tests.cpp'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=30)
