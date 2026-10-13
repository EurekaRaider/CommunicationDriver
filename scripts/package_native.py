"""Stage a native RID directory and licenses; resolve local dynamic dependencies recursively."""
import argparse, json, os, re, shutil, subprocess, sys
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('--build',required=True);p.add_argument('--rid',required=True);p.add_argument('--out',default='artifacts/package')
a=p.parse_args();root=Path(__file__).resolve().parents[1];build=Path(a.build).resolve();out=Path(a.out).resolve();native=out/'runtimes'/a.rid/'native';native.mkdir(parents=True,exist_ok=True)
name='communicationdriver.dll' if os.name=='nt' else 'libcommunicationdriver.dylib' if sys.platform=='darwin' else 'libcommunicationdriver.so'
candidates=list(build.rglob(name));candidates=[x for x in candidates if '_deps' not in x.parts]
if not candidates:raise SystemExit('Native library not found')
source=candidates[0].resolve();shutil.copy2(source,native/name)
queue=[native/name];seen={source.name}
while queue and os.name!='nt':
 lib=queue.pop()
 if sys.platform=='darwin':
  lines=subprocess.check_output(['otool','-L',str(lib)],text=True).splitlines()[1:]
  deps=[line.strip().split(' (')[0] for line in lines]
 else:
  text=subprocess.check_output(['ldd',str(lib)],text=True)
  deps=re.findall(r'=> (/[^ ]+)',text)
 for dep in deps:
  path=Path(dep)
  if sys.platform=='darwin' and (dep.startswith('/usr/lib/') or dep.startswith('/System/')):continue
  if sys.platform!='darwin' and re.search(r'/(libc|libm|libpthread|libdl|librt|libstdc\+\+|libgcc_s|ld-linux)[.-]',dep):continue
  if not path.is_absolute() or not path.exists():continue
  if path.name not in seen:
   seen.add(path.name);shutil.copy2(path.resolve(),native/path.name);queue.append(native/path.name)
  if sys.platform=='darwin':subprocess.check_call(['install_name_tool','-change',dep,'@loader_path/'+path.name,str(lib)])
 if sys.platform=='darwin':subprocess.check_call(['install_name_tool','-id','@rpath/'+lib.name,str(lib)])
if sys.platform=='darwin':
 for lib in native.glob('*.dylib'):subprocess.check_call(['codesign','--force','--sign','-',str(lib)])
licenses=out/'licenses';licenses.mkdir(exist_ok=True)
shutil.copy2(root/'LICENSE',licenses/'CommunicationDriver-MIT.txt')
for dep,names in [('boost',['LICENSE_1_0.txt']),('absl',['LICENSE']),('re2',['LICENSE']),('ssh2',['COPYING'])]:
 folder=build/'_deps'/(dep+'-src')
 for n in names:
  if (folder/n).exists():shutil.copy2(folder/n,licenses/(dep+'-'+n+'.txt'))
# OpenSSL is a dynamically linked platform dependency on Unix.
if os.name!='nt':
 openssl_license=licenses/'OpenSSL-NOTICE.txt'
 openssl_license.write_text('OpenSSL runtime is redistributed under Apache-2.0. Full license: https://www.openssl.org/source/license.html\n',encoding='utf-8')
(out/'native-build.json').write_text(json.dumps({'rid':a.rid,'library':name,'runtime_files':sorted(x.name for x in native.iterdir())},indent=2)+'\n')
print(native)
