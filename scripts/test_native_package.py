"""Build and run independent C/C++ clients using only staged headers and libraries."""
import argparse, os, shlex, subprocess, sys
from pathlib import Path
parser=argparse.ArgumentParser();parser.add_argument('--rid',required=True);parser.add_argument('--package',default='artifacts/package');args=parser.parse_args()
root=Path(__file__).resolve().parents[1];package=Path(args.package).resolve();native=package/'runtimes'/args.rid/'native';out=root/'artifacts'/'native-consumers'/args.rid;out.mkdir(parents=True,exist_ok=True)
environment=os.environ.copy();environment['PATH']=str(native)+os.pathsep+environment['PATH'];environment['LD_LIBRARY_PATH']=str(native);environment['DYLD_LIBRARY_PATH']=str(native)
for language in ['c','cpp']:
 source=root/'examples'/language/('main.c' if language=='c' else 'main.cpp');binary=out/('consumer-'+language+('.exe' if os.name=='nt' else ''))
 if os.name=='nt':
  command=['cl','/nologo','/utf-8','/MT','/I'+str(package/'include'),str(source),'/Fo'+str(out/(language+'.obj')),'/Fe'+str(binary)]
  if language=='cpp':command+=['/std:c++latest','/EHsc']
  command+=['/link',str(package/'lib'/args.rid/'communicationdriver_native.lib')]
 else:
  compiler=os.environ.get('CC' if language=='c' else 'CXX','cc' if language=='c' else 'c++')
  command=shlex.split(compiler)+shlex.split(os.environ.get('CFLAGS' if language=='c' else 'CXXFLAGS',''))
  if language=='cpp':command+=['-std=c++23']
  command+=['-I'+str(package/'include'),str(source),'-L'+str(native),'-lcommunicationdriver_native','-Wl,-rpath,'+str(native),'-o',str(binary)]+shlex.split(os.environ.get('LDFLAGS',''))
 subprocess.check_call(command);subprocess.check_call([str(binary)],env=environment)
print('PASS staged native C and C++ consumers: '+args.rid)
