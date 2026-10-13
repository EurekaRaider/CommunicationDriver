"""Run the native integration suite against an isolated loopback OpenSSH server."""
import argparse, getpass, os, subprocess, threading
from collections import deque
from pathlib import Path
parser=argparse.ArgumentParser();parser.add_argument('--build',default='build');parser.add_argument('--user-server',action='store_true',help='Run sshd as the current user without sudo');parser.add_argument('--repeat',type=int,default=1);args=parser.parse_args()
if args.repeat<1:parser.error('--repeat must be positive')
root=Path(__file__).resolve().parents[1];fixture=root/'artifacts'/'ssh-fixture';fixture.mkdir(parents=True,exist_ok=True)
for name in ['host_key','client_key']:
 key=fixture/name
 if not key.exists():subprocess.check_call(['ssh-keygen','-q','-t','rsa','-b','3072','-m','PEM','-N','','-C','CommunicationDriver-test','-f',str(key)])
configuration=fixture/'sshd_config';pam='yes' if os.uname().sysname=='Linux' else 'no';configuration.write_text('Port 22482\nListenAddress 127.0.0.1\nHostKey '+str(fixture/'host_key')+'\nAuthorizedKeysFile '+str(fixture/'client_key.pub')+'\nPidFile '+str(fixture/'sshd.pid')+'\nPasswordAuthentication no\nKbdInteractiveAuthentication no\nUsePAM '+pam+'\nStrictModes no\nSubsystem sftp internal-sftp\nLogLevel INFO\n')
subprocess.run(['sudo','-n','mkdir','-p','/run/sshd'],check=True) if os.uname().sysname=='Linux' and not args.user_server else None
server=subprocess.Popen(([] if args.user_server else ['sudo','-n'])+['/usr/sbin/sshd','-D','-e','-f',str(configuration)],stderr=subprocess.PIPE,text=True)
def stop():
 if server.poll() is None:
  if args.user_server:server.terminate()
  else:subprocess.run(['sudo','-n','kill','-TERM',str(server.pid)],check=False)
watchdog=threading.Timer(15,stop);watchdog.start();passed=False
try:
 startup=[]
 for line in server.stderr:
  startup.append(line)
  if 'Server listening on' in line:break
 else:raise RuntimeError('Isolated OpenSSH server failed to start: '+''.join(startup))
 watchdog.cancel();messages=deque(maxlen=100)
 def drain():
  for message in server.stderr:messages.append(message)
 reader=threading.Thread(target=drain,daemon=True);reader.start()
 environment=os.environ.copy();environment['CD_SSH_USER']=getpass.getuser();environment['CD_SSH_PRIVATE_KEY']=str(fixture/'client_key');environment['CD_SSH_FINGERPRINT']=subprocess.check_output(['ssh-keygen','-lf',str(fixture/'host_key.pub'),'-E','sha256'],text=True).split()[1]
 for _ in range(args.repeat):subprocess.run([str(root/args.build/'cd_integration_tests'),'--ssh'],env=environment,check=True,timeout=120)
 passed=True
finally:
 watchdog.cancel();stop();server.wait(timeout=10)
 if 'reader' in locals():reader.join(timeout=5)
 if not passed:print(''.join(messages) if 'messages' in locals() else server.stderr.read())
