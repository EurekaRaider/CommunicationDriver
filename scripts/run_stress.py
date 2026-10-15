"""Run a hardware-free workload with a watchdog and resource samples."""
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
parser.add_argument('--process-tree', action='store_true', help='sample cross-platform resources and check observed child cleanup (requires psutil)')
parser.add_argument('--idle-timeout', type=float, default=0, help='fail if the workload log stops advancing for this many seconds')
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
idle_timed_out = False
survivors = []
observed = set()
if args.process_tree:
    import psutil
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
    root = psutil.Process(process.pid) if args.process_tree else None
    last_progress = time.monotonic()
    last_size = 0
    next_report = 0
    try:
        while process.poll() is None:
            elapsed = time.monotonic() - started
            if elapsed >= args.timeout:
                timed_out = True
                break
            size = log.stat().st_size
            if size != last_size:
                last_progress, last_size = time.monotonic(), size
            if args.idle_timeout and time.monotonic() - last_progress >= args.idle_timeout:
                idle_timed_out = True
                break
            sample = {'elapsed_s': round(elapsed, 3)}
            if root is not None:
                try:
                    children = root.children(recursive=True)
                    observed.update(children)
                    observed = {child for child in observed if child.is_running()}
                    sample.update(memory_bytes=root.memory_info()._asdict(), threads=root.num_threads(),
                                  cpu_seconds=sum(root.cpu_times()[:2]), children=len(children))
                    sample['handles' if os.name == 'nt' else 'fds'] = root.num_handles() if os.name == 'nt' else root.num_fds()
                    child_rss = 0
                    for child in children:
                        try:
                            child_rss += child.memory_info().rss
                        except psutil.NoSuchProcess:
                            pass
                    sample['children_rss_bytes'] = child_rss
                except psutil.NoSuchProcess:
                    pass
            elif os.name != 'nt':
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
            if root is not None and elapsed >= next_report:
                print(json.dumps({'workload': args.output.stem, 'sample': sample}), flush=True)
                args.output.with_suffix('.samples.json').write_text(json.dumps(samples) + '\n')
                next_report = elapsed + 60
            try:
                process.wait(timeout=min(args.interval, max(0.01, args.timeout - elapsed)))
            except subprocess.TimeoutExpired:
                pass
    finally:
        if process.poll() is None:
            if root is not None:
                try:
                    observed.update(root.children(recursive=True))
                except psutil.NoSuchProcess:
                    pass
            if os.name == 'nt':
                process.kill()
            else:
                os.killpg(process.pid, signal.SIGKILL)
        process.wait()
        if root is not None:
            _, alive = psutil.wait_procs(list(observed), timeout=5)
            survivors = [{'pid': child.pid, 'created': child.create_time()} for child in alive]
            for child in alive:
                try:
                    child.kill()
                except psutil.NoSuchProcess:
                    pass
            psutil.wait_procs(alive, timeout=5)
result = {'command': command, 'platform': platform.platform(), 'exit_code': process.returncode,
          'timed_out': timed_out, 'elapsed_s': round(time.monotonic() - started, 3),
          'idle_timed_out': idle_timed_out, 'surviving_observed_children': survivors,
          'artifact_sha256': hashes,
          'samples': samples, 'log': str(log)}
args.output.write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps({key: value for key, value in result.items() if key != 'samples'}))
raise SystemExit(124 if timed_out or idle_timed_out else process.returncode or (1 if survivors else 0))
