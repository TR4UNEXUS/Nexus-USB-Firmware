from pathlib import Path
import re,subprocess,tempfile
T=Path(__file__).resolve().parent;S=T.parents[1];L=S/'libraries/libstratosphere'
strip=lambda s:re.sub(r'^#(?:include|pragma)[^\n]*\n','',s,flags=re.M)
production=(L/'source/ncm/ncm_usb_map_backup.cpp').read_text()
text=(T/'usb_map_backup_fixture.hpp').read_text()+strip((L/'source/ncm/ncm_usb_limits.hpp').read_text())+strip((L/'include/stratosphere/ncm/ncm_usb_map_backup.hpp').read_text())+strip(production)+(T/'usb_map_backup_cases.cpp').read_text()
with tempfile.TemporaryDirectory(prefix='nexus-map-backup-') as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(text)
 for opt in ('-O0','-O2'):
  subprocess.run(['g++','-std=c++23',opt,'-Wall','-Wextra','-Werror','-Wno-deprecated-declarations','-fsanitize=undefined','-fno-sanitize-recover=all',str(p/'test.cpp'),'-lcrypto','-o',str(p/'test')],check=True)
  subprocess.run([str(p/'test')],check=True,timeout=30)
