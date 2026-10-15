"""Run independent native, mixed-transport and managed hour workloads concurrently."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import subprocess
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build', type=Path, default=Path('build'))
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--seconds', type=int, default=3600)
parser.add_argument('--native-only', action='store_true')
args = parser.parse_args()
if args.seconds <= 0:
    parser.error('seconds must be positive')
args.output.mkdir(parents=True, exist_ok=True)
suffix = '.exe' if os.name == 'nt' else ''
commands = {
    'native': [str((args.build / ('cd_stress_tests' + suffix)).resolve())],
    'mixed': [str((args.build / ('cd_tests' + suffix)).resolve())],
}
if not args.native_only:
    commands['managed'] = ['dotnet', 'tests/dotnet/Stress/bin/Release/net10.0/Stress.dll']

def run(item):
    name, command = item
    command += ['--soak-seconds', str(args.seconds)]
    result = subprocess.run([sys.executable, 'scripts/run_stress.py', '--output', str(args.output / (name + '.json')),
                             '--library-dir', str(args.build.resolve()), '--timeout', str(args.seconds + 300),
                             '--interval', '5', '--process-tree', '--idle-timeout', '120', '--', *command])
    return name, result.returncode

with ThreadPoolExecutor(max_workers=len(commands)) as pool:
    results = dict(pool.map(run, commands.items()))
print(json.dumps(results), flush=True)
raise SystemExit(1 if any(results.values()) else 0)
