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
queue=[native/name];seen={source.name};origins={native/name:source}
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
  if sys.platform=='darwin' and dep.startswith('@loader_path/'):
   path=origins[lib].parent/dep.removeprefix('@loader_path/')
  if sys.platform=='darwin' and dep.startswith('@rpath/'):
   commands=subprocess.check_output(['otool','-l',str(origins[lib])],text=True)
   rpaths=re.findall(r'cmd LC_RPATH\n\s+cmdsize \d+\n\s+path (.*?) \(offset',commands)
   candidates=[Path(r.replace('@loader_path',str(origins[lib].parent)))/dep.removeprefix('@rpath/') for r in rpaths]
   path=next((x for x in candidates if x.exists()),path)
  if not path.is_absolute() or not path.exists():
   raise SystemExit('Unresolved runtime dependency: '+dep)
  if path.name not in seen:
   seen.add(path.name);shutil.copy2(path.resolve(),native/path.name);origins[native/path.name]=path.resolve();queue.append(native/path.name)
  if sys.platform=='darwin':subprocess.check_call(['install_name_tool','-change',dep,'@loader_path/'+path.name,str(lib)])
 if sys.platform=='darwin':subprocess.check_call(['install_name_tool','-id','@rpath/'+lib.name,str(lib)])
if sys.platform=='darwin':
 for lib in native.glob('*.dylib'):subprocess.check_call(['codesign','--force','--sign','-',str(lib)])
licenses=out/'licenses';licenses.mkdir(exist_ok=True)
shutil.copy2(root/'LICENSE',licenses/'CommunicationDriver-MIT.txt')
shutil.copytree(root/'include',out/'include',dirs_exist_ok=True)
if (root/'third_party/licenses').exists():
 for license in (root/'third_party/licenses').iterdir():shutil.copy2(license,licenses/license.name)
for dep,names in [('boost',['LICENSE_1_0.txt']),('absl',['LICENSE']),('re2',['LICENSE']),('ssh2',['COPYING'])]:
 folder=build/'_deps'/(dep+'-src')
 for n in names:
  if (folder/n).exists():shutil.copy2(folder/n,licenses/(dep+'-'+n+'.txt'))
# OpenSSL is a dynamically linked platform dependency on Unix.
if os.name!='nt':
 if (build/'_deps/absl-src/LICENSE').exists():shutil.copy2(build/'_deps/absl-src/LICENSE',licenses/'OpenSSL-Apache-2.0.txt')
 openssl_license=licenses/'OpenSSL-NOTICE.txt'
 openssl_license.write_text('OpenSSL runtime is redistributed under Apache-2.0. Full license: https://www.openssl.org/source/license.html\n',encoding='utf-8')
(out/'native-build.json').write_text(json.dumps({'rid':a.rid,'library':name,'runtime_files':sorted(x.name for x in native.iterdir())},indent=2)+'\n')
print(native)
