"""Run a hardware-free workload with a watchdog and Unix resource samples."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import signal
import shutil
import subprocess
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--timeout', type=float, default=360)
parser.add_argument('--interval', type=float, default=5)
parser.add_argument('--library-dir', type=Path, help='native library directory (also works with macOS system Python)')
parser.add_argument('command', nargs=argparse.REMAINDER)
args = parser.parse_args()
command = args.command[1:] if args.command[:1] == ['--'] else args.command
if not command or args.timeout <= 0 or args.interval <= 0:
    parser.error('supply a command, positive timeout and interval')
args.output.parent.mkdir(parents=True, exist_ok=True)
log = args.output.with_suffix('.log')
samples = []
started = time.monotonic()
timed_out = False
binary = Path(shutil.which(command[0]) or command[0]).resolve()
hashes = {str(binary): hashlib.sha256(binary.read_bytes()).hexdigest()}
environment = os.environ.copy()
library_dir = args.library_dir.resolve() if args.library_dir else binary.parent
if args.library_dir:
    variable = 'PATH' if os.name == 'nt' else 'DYLD_LIBRARY_PATH' if platform.system() == 'Darwin' else 'LD_LIBRARY_PATH'
    environment[variable] = os.pathsep.join(value for value in (str(library_dir), environment.get(variable)) if value)
for argument in command[1:]:
    if argument.endswith('.dll') and Path(argument).is_file():
        assembly = Path(argument).resolve()
        hashes[str(assembly)] = hashlib.sha256(assembly.read_bytes()).hexdigest()
for name in ('libcommunicationdriver_native.dylib', 'libcommunicationdriver_native.so', 'communicationdriver_native.dll'):
    library = library_dir / name
    if library.exists():
        hashes[str(library)] = hashlib.sha256(library.read_bytes()).hexdigest()
with log.open('w') as output:
    process = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT, env=environment,
                               start_new_session=os.name != 'nt')
    try:
        while process.poll() is None:
            elapsed = time.monotonic() - started
            if elapsed >= args.timeout:
                timed_out = True
                break
            sample = {'elapsed_s': round(elapsed, 3)}
            if os.name != 'nt':
                info = subprocess.run(['ps', '-p', str(process.pid), '-o', 'rss=,%cpu='],
                                      capture_output=True, text=True, timeout=5).stdout.split()
                if len(info) == 2:
                    sample.update(rss_kib=int(info[0]), cpu_percent=float(info[1]))
                if platform.system() == 'Darwin':
                    threads = subprocess.run(['ps', '-M', str(process.pid)], capture_output=True,
                                             text=True, timeout=5).stdout.splitlines()
                    sample['threads'] = max(0, len(threads) - 1)
                    fds = subprocess.run(['/usr/sbin/lsof', '-a', '-p', str(process.pid), '-Ff'],
                                         capture_output=True, text=True, timeout=5).stdout.splitlines()
                    sample['fds'] = sum(line[1:].isdigit() for line in fds if line.startswith('f'))
                elif platform.system() == 'Linux':
                    try:
                        sample['threads'] = len(list(Path(f'/proc/{process.pid}/task').iterdir()))
                        sample['fds'] = len(list(Path(f'/proc/{process.pid}/fd').iterdir()))
                    except FileNotFoundError:
                        pass
            samples.append(sample)
            try:
                process.wait(timeout=min(args.interval, max(0.01, args.timeout - elapsed)))
            except subprocess.TimeoutExpired:
                pass
    finally:
        if process.poll() is None:
            if os.name == 'nt':
                process.kill()
            else:
                os.killpg(process.pid, signal.SIGKILL)
        process.wait()
result = {'command': command, 'platform': platform.platform(), 'exit_code': process.returncode,
          'timed_out': timed_out, 'elapsed_s': round(time.monotonic() - started, 3),
          'artifact_sha256': hashes,
          'samples': samples, 'log': str(log)}
args.output.write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps({key: value for key, value in result.items() if key != 'samples'}))
raise SystemExit(124 if timed_out else process.returncode)
