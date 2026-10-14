#!/usr/bin/env python3
"""Compare identical public-API workloads against two native and managed builds."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import statistics
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build', type=Path, required=True)
parser.add_argument('--before-native', type=Path, required=True)
parser.add_argument('--after-native', type=Path, required=True)
parser.add_argument('--before-managed', type=Path, required=True)
parser.add_argument('--after-managed', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--repeat', type=int, default=3)
args = parser.parse_args()
if args.repeat < 1:
    parser.error('--repeat must be positive')
exe = '.exe' if os.name == 'nt' else ''
build = args.build.resolve()
jobs = [([str(build / ('cd_stress_tests' + exe)), '--performance'], 'native'),
        ([str(build / ('cd_tests' + exe)), '--text-performance'], 'native')]
jobs += [([str(build / ('cd_tests' + exe)), '--batch-cancel-count', str(count)], 'native') for count in (2000, 4000, 8000)]
jobs += [([], 'managed')]
samples = {}
logs = []
for command, kind in jobs:
    for repeat in range(args.repeat):
        for variant in (('before', 'after') if repeat % 2 == 0 else ('after', 'before')):
            env = os.environ.copy()
            directory = getattr(args, variant + '_native').resolve()
            key = 'PATH' if os.name == 'nt' else 'DYLD_LIBRARY_PATH' if platform.system() == 'Darwin' else 'LD_LIBRARY_PATH'
            env[key] = str(directory) + os.pathsep + env.get(key, '')
            run = command if kind == 'native' else ['dotnet', str(getattr(args, variant + '_managed').resolve())]
            result = subprocess.run(run, env=env, capture_output=True, text=True, timeout=120)
            logs.append({'variant': variant, 'command': run, 'stdout': result.stdout, 'stderr': result.stderr, 'returncode': result.returncode})
            if result.returncode:
                raise RuntimeError(result.stdout + result.stderr)
            for line in result.stdout.splitlines():
                match = re.match(r'(.*?) (?:elapsed_)?us=(\d+)(?: allocated_bytes=(\d+))?$', line)
                if match:
                    label = match[1].removeprefix('METRIC ')
                    values = samples.setdefault(label, {}).setdefault(variant, {'us': [], 'allocated_bytes': []})
                    values['us'].append(int(match[2]))
                    if match[3]:
                        values['allocated_bytes'].append(int(match[3]))
            print(f'{variant} {kind} round={repeat + 1} done', flush=True)
summary = {}
for label, variants in samples.items():
    summary[label] = {variant: {'median_us': statistics.median(values['us']), **values} for variant, values in variants.items()}
    before = summary[label]['before']['median_us']
    summary[label]['time_reduction_percent'] = 100 * (1 - summary[label]['after']['median_us'] / before)
args.output.parent.mkdir(parents=True, exist_ok=True)
report = {'platform': platform.platform(), 'repeat': args.repeat, 'results': summary, 'runs': logs,
          'sha256': {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in (build / ('cd_tests' + exe), build / ('cd_stress_tests' + exe), args.before_managed, args.after_managed)}}
args.output.write_text(json.dumps(report, indent=2, ensure_ascii=False) + '\n')
for label, values in summary.items():
    print(label, values['before']['median_us'], '->', values['after']['median_us'])
