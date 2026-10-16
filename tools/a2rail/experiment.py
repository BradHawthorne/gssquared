"""Build/run a declared raw CPU fixture and retain context-bound efficiency evidence.

An additive Rosetta companion, not a new Rosetta v3 CLI feature. Build tools
are explicit executable/argv lists; no shell. Each invocation owns a fresh
directory and a fresh machine. A raw load is never a GS/OS boot claim.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
from types import SimpleNamespace
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'clients/python/src'))
from gs2debug.mcp import Backend
from gs2debug.observations import observe
from gs2debug import RequestPending, ProtocolError


def digest(path):
    value = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024*1024), b''):
            value.update(chunk)
    return value.hexdigest()


def encoded(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'), allow_nan=False)


def child(root, name):
    if not isinstance(name, str) or not name or Path(name).is_absolute() or ':' in name or '..' in Path(name).parts:
        raise ValueError('artifact name must be relative to the experiment')
    path = (root / name).resolve()
    if not path.is_relative_to(root):
        raise ValueError('artifact escapes experiment directory')
    return path


def integer(value, low, high):
    if type(value) is not int or not low <= value <= high:
        raise ValueError(f'expected integer in {low}..{high}')
    return value


def validate(plan):
    allowed = {'schema', 'workload', 'platform', 'files', 'build', 'image', 'registers', 'entry', 'target', 'attempts', 'checks'}
    if not isinstance(plan, dict) or set(plan)-allowed or plan.get('schema') != 'a2experiment-plan-v1':
        raise ValueError('invalid experiment schema or fields')
    if not isinstance(plan.get('workload'), dict) or set(plan['workload']) != {'id', 'case'} or any(not isinstance(v, str) or not v for v in plan['workload'].values()):
        raise ValueError('workload requires nonempty id and case strings')
    integer(plan.get('platform'), 0, 100)
    for key in ('entry', 'target'):
        integer(plan.get(key), 0, 0xffffff)
    integer(plan.get('attempts'), 1, 10000000)
    if not isinstance(plan.get('image'), dict) or set(plan['image']) != {'path', 'address', 'format'} or plan['image']['format'] != 'raw':
        raise ValueError('explicit raw image path/address/format required; OMF needs an independent load plan or OS loader')
    integer(plan['image']['address'], 0, 0xffffff)
    if not isinstance(plan.get('registers', {}), dict) or set(plan.get('registers', {})) - {'a', 'x', 'y', 'sp', 'p', 'e', 'd'}:
        raise ValueError('unsupported explicit CPU registers')
    for key, value in plan.get('registers', {}).items():
        integer(value, 0, 1 if key == 'e' else 255 if key in ('p', 'dbr') else 65535)
    checks = plan.get('checks')
    if not isinstance(checks, list) or not 1 <= len(checks) <= 16:
        raise ValueError('one to sixteen independent result memory checks required')
    for check in checks:
        if not isinstance(check, dict) or set(check) != {'address', 'bytes'}:
            raise ValueError('check requires address/bytes')
        integer(check['address'], 0, 0xffffff)
        if not isinstance(check['bytes'], list) or not 1 <= len(check['bytes']) <= 256:
            raise ValueError('check requires 1..256 expected bytes')
        for byte in check['bytes']:
            integer(byte, 0, 255)
        if check['address'] + len(check['bytes']) > 0x1000000:
            raise ValueError('check exceeds CPU address space')
    if not isinstance(plan.get('files'), list) or not 1 <= len(plan['files']) <= 64:
        raise ValueError('explicit digest-bound build inputs required')
    for row in plan['files']:
        if not isinstance(row, dict) or set(row) != {'source', 'path', 'sha256'} or not Path(row['source']).is_absolute():
            raise ValueError('input requires absolute source, relative path and sha256')
        if not isinstance(row['sha256'], str) or len(row['sha256']) != 64 or any(c not in '0123456789abcdef' for c in row['sha256']):
            raise ValueError('input sha256 must be lowercase hex')
    if not isinstance(plan.get('build', []), list) or len(plan.get('build', [])) > 16:
        raise ValueError('build must be a bounded list of explicit argument lists')
    for argv in plan.get('build', []):
        if not isinstance(argv, list) or not argv or any(not isinstance(arg, str) or '\0' in arg for arg in argv) or not Path(argv[0]).is_absolute():
            raise ValueError('build commands require absolute executable and string argv; no shell')


def run(plan, executable, directory):
    validate(plan)
    directory = Path(directory).resolve()
    # Validate all destination paths before creating output or launching tools.
    for row in plan['files']:
        child(directory, row['path'])
    image = child(directory, plan['image']['path'])
    directory.mkdir(parents=True, exist_ok=False)
    (directory/'plan.json').write_text(encoded(plan)+'\n', encoding='utf-8')
    report = {'schema': 'a2experiment-run-v1', 'ok': False, 'outcome': 'setup-failure', 'workload': plan['workload'],
              'platform': plan['platform'], 'checks': plan['checks'], 'build': [], 'inputs': plan['files'],
              'claim': 'declared raw CPU fixture on the selected emulator; not physical hardware or application boot acceptance'}
    contract = {'workload': plan['workload'], 'checks': plan['checks']}
    report['workload_sha256'] = hashlib.sha256(encoded(contract).encode()).hexdigest()
    backend = None
    try:
        for row in plan['files']:
            source = Path(row['source']); target = child(directory, row['path'])
            if digest(source) != row['sha256']:
                raise ValueError('input identity mismatch: '+str(source))
            target.parent.mkdir(parents=True, exist_ok=True)
            with target.open('xb') as stream:
                stream.write(source.read_bytes())
            if digest(target) != row['sha256']:
                raise ValueError('input changed during staging')
        for index, argv in enumerate(plan.get('build', [])):
            tool = Path(argv[0]).resolve(); before = digest(tool)
            command = [arg.replace('{work}', str(directory)) for arg in argv]
            with (directory/f'build-{index}.stdout').open('wb') as stdout, (directory/f'build-{index}.stderr').open('wb') as stderr:
                result = subprocess.run(command, cwd=directory, stdout=stdout, stderr=stderr, timeout=120)
            report['build'].append({'argv': command, 'tool_sha256': before, 'exit_code': result.returncode})
            if digest(tool) != before or result.returncode:
                raise ValueError('build failed or executable identity changed')
        size = image.stat().st_size
        if not size or size > 512*1024 or plan['image']['address']+size > 0x1000000:
            raise ValueError('raw CPU image range invalid')
        if any(max(plan['image']['address'], check['address']) < min(plan['image']['address']+size, check['address']+len(check['bytes'])) for check in plan['checks']):
            raise ValueError('result checks overlap loaded program; declare separate fixture output')
        report['image'] = {'path': str(image), 'sha256': digest(image), 'size': size, 'address': plan['image']['address']}
        args = SimpleNamespace(exe=Path(executable).resolve(), socket=None, out_dir=directory/'machine', out_root=None,
                               artifact_root=None, config=None, platform=plan['platform'], seed=1, rail=[])
        backend = Backend(args); backend.open()
        client = backend.client
        def command(text, check=True):
            try:
                return client.agent_command(text, check=check, timeout=30)
            except RequestPending:
                report.setdefault('pending_recoveries', []).append(dict(client.pending_request, command=text))
                return client.wait_agent_command(check=check, timeout=30)
        command('context on 256')
        command(f'load {plan["image"]["address"]:06X} "{image.as_posix()}"')
        for check in plan['checks']:
            poison = ' '.join(f'{byte ^ 255:02X}' for byte in check['bytes'])
            command(f'poke {check["address"]:06X} '+poison)
        for key, value in plan.get('registers', {}).items():
            command(f'setreg {key} {value:X}')
        command(f'setreg pc {plan["entry"]:06X}')
        report['before'] = command('context overview')['data']
        execution = command(f'run-until {plan["target"]:06X} {plan["attempts"]}', check=False)
        report['execution'] = execution
        report['observations'] = []
        for check in plan['checks']:
            report['observations'].append(observe(client, 'gs2_memory_read', {'domain': 'main', 'address': check['address'], 'length': len(check['bytes'])}))
        first = plan['checks'][0]
        report['negative_control'] = command(f'assert peek:{first["address"]:06X}==${first["bytes"][0]^1:02X}', check=False)
        report['after'] = command('context overview')['data']
        snapshot = directory/'machine'/'context.json'
        report['context_snapshot'] = command('context snapshot "'+snapshot.as_posix()+'"')['data']
        report['context_snapshot_sha256'] = digest(snapshot)
        completed = execution.get('data') or {}
        reached = completed.get('completed') and completed.get('target_reached') and int(completed.get('instructions', '0')) > 0
        correct = all(item['bytes'] == expected['bytes'] and not item['unreadable_bytes'] for item, expected in zip(report['observations'], plan['checks']))
        negative = report['negative_control'].get('ok') is False
        report['ok'] = bool(reached and correct and negative)
        report['outcome'] = 'pass' if report['ok'] else 'incomplete-execution' if not reached else 'assertion-mismatch' if not correct else 'negative-control-failure'
        report['executable_sha256'] = backend.executable_sha256
        report['pins'] = {'seed': 1, 'faketime': 1000000000, 'bram': 'default'}
    except Exception as error:
        report['error'] = str(error)
        if isinstance(error, (RequestPending, OSError, ProtocolError)) and backend is not None:
            report['outcome'] = 'transport-or-native-refusal'
            report['pending_request'] = backend.client.pending_request
            report['outcome_unknown'] = isinstance(error, RequestPending) or isinstance(error, OSError)
    finally:
        if backend is not None:
            try:
                backend.close()
            except Exception as error:
                report['ok'] = False
                report['outcome'] = 'shutdown-failure'
                report['shutdown_error'] = str(error)
            if backend.process is not None:
                report['exit_code'] = backend.process.poll()
                if report['exit_code'] != 0:
                    report['ok'] = False
                    report['outcome'] = 'shutdown-failure'
        path = directory/'run.json'
        path.write_text(encoded(report)+'\n', encoding='utf-8')
    return report


def compare(left, right):
    for report in (left, right):
        if report.get('schema') != 'a2experiment-run-v1' or report.get('ok') is not True or report.get('outcome') != 'pass' or not report.get('checks'):
            raise ValueError('efficiency comparison requires two passing experiment records')
        execution = report.get('execution', {}).get('data', {})
        if execution.get('schema') != 'a2execution-v1' or execution.get('cycle_model') != 'in-command' or not execution.get('completed') or not execution.get('target_reached'):
            raise ValueError('incomplete or incompatible native measurement')
        if not report.get('negative_control', {}).get('ok') is False:
            raise ValueError('missing failing negative control')
        before, after = report['before']['stamp'], report['after']['stamp']
        if before['context_session'] != after['context_session'] or before['clock_epoch'] != after['clock_epoch']:
            raise ValueError('measurement crosses a session or observed clock discontinuity')
        if int(after['cpu_cycles'])-int(before['cpu_cycles']) != int(execution['cycles']):
            raise ValueError('context CPU interval disagrees with execution receipt')
        if not all(item['bytes'] == expected['bytes'] and item['unreadable_bytes'] == 0 for item, expected in zip(report['observations'], report['checks'])) or len(report['observations']) != len(report['checks']):
            raise ValueError('incorrect measured output')
    for key in ('workload_sha256', 'platform', 'executable_sha256', 'pins'):
        if left[key] != right[key]:
            raise ValueError('incomparable experiment '+key)
    if left['before']['data']['clock'] != right['before']['data']['clock']:
        # Absolute frame/phase state is part of this contract, not merely MHz.
        raise ValueError('clock mode/phase setup differs')
    metrics = {}
    for name in ('cycles', 'instructions'):
        values = [report['execution']['data'][name] for report in (left, right)]
        if any(not isinstance(value, str) or not value.isascii() or not value.isdecimal() or int(value) <= 0 for value in values):
            raise ValueError('invalid or zero measurement '+name)
        a, b = map(int, values)
        metrics[name] = {'baseline': str(a), 'candidate': str(b), 'delta': str(b-a), 'reduction_percent': 100*(a-b)/a}
    metrics['image_bytes'] = {'baseline': left['image']['size'], 'candidate': right['image']['size'], 'delta': right['image']['size']-left['image']['size']}
    for name in ('master_14m_ticks', 'video_cycles'):
        intervals = [int(report['after']['stamp'][name])-int(report['before']['stamp'][name]) for report in (left, right)]
        if any(value < 0 for value in intervals):
            raise ValueError('native clock moved backward during measurement')
        a, b = intervals
        metrics[name] = {'baseline': str(a), 'candidate': str(b), 'delta': str(b-a), 'reduction_percent': 100*(a-b)/a if a else None}
    return {'schema': 'a2efficiency-comparison-v1', 'ok': True, 'workload': left['workload'], 'metrics': metrics,
            'bounds': {'kind': 'observed-pair', 'proven_floor': False, 'proven_ceiling': False},
            'hardware_status': 'emulator measurements; no physical-IIgs result implied',
            'model': 'same declared workload/results, emulator, platform, pins and clock setup; correctness and negative controls required'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    runner = sub.add_parser('run'); runner.add_argument('--plan', type=Path, required=True); runner.add_argument('--exe', type=Path, required=True); runner.add_argument('--out', type=Path, required=True)
    comparison = sub.add_parser('compare'); comparison.add_argument('baseline', type=Path); comparison.add_argument('candidate', type=Path); comparison.add_argument('--out', type=Path)
    args = parser.parse_args()
    try:
        if args.command == 'run':
            report = run(json.loads(args.plan.read_text(encoding='utf-8')), args.exe, args.out)
        else:
            records = []
            for path in (args.baseline, args.candidate):
                record = json.loads(path.read_text(encoding='utf-8'))
                if digest(record['image']['path']) != record['image']['sha256'] or digest(record['context_snapshot']['path']) != record['context_snapshot_sha256']:
                    raise ValueError('retained experiment artifact identity mismatch')
                records.append(record)
            report = compare(*records)
            if args.out:
                args.out.write_text(encoded(report)+'\n', encoding='utf-8')
        print(encoded(report))
        return 0 if report['ok'] else 1
    except (OSError, ValueError, KeyError, TypeError) as error:
        print('experiment: '+str(error), file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
