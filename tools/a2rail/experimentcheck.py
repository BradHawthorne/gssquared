"""Known-pass/fail efficiency evidence; optional real Rosetta assembly build."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parent))
from p0check import check
import experiment


def run(exe, assembler=None, out=None):
    temporary = tempfile.TemporaryDirectory(prefix='a2experiment-') if out is None else None
    root = Path(temporary.name if temporary else out).resolve()
    root.mkdir(parents=True, exist_ok=True)
    comparisons = []
    try:
        for platform in (3, 5):
            for byte in (0, 0x7f, 0x80, 0xff):
                records = []
                for name, extra in (('baseline', True), ('candidate', False)):
                    tag = f'p{platform}-{byte:02x}-{name}'
                    if assembler:
                        source = root/(tag+'.s')
                        source.write_text(' ORG $0800\n LDA #$'+f'{byte:02X}\n'+(' NOP\n' if extra else '')+' STA $0900\n', encoding='utf-8')
                        files = [{'source': str(source), 'path': 'fixture.s', 'sha256': experiment.digest(source)}]
                        build = [[str(assembler), '--cpu', '65C02', '--format', 'bin', 'fixture.s', '-o', 'fixture.bin']]
                    else:
                        source = root/(tag+'.bin')
                        source.write_bytes(bytes([0xa9, byte] + ([0xea] if extra else []) + [0x8d, 0, 9]))
                        files = [{'source': str(source), 'path': 'fixture.bin', 'sha256': experiment.digest(source)}]
                        build = []
                    plan = {'schema': 'a2experiment-plan-v1', 'workload': {'id': 'store-one-byte', 'case': f'input-{byte:02x}'},
                            'platform': platform, 'files': files, 'build': build,
                            'image': {'path': 'fixture.bin', 'address': 0x800, 'format': 'raw'},
                            'registers': {'e': 1, 'p': 0x34}, 'entry': 0x800, 'target': 0x806 if extra else 0x805,
                            'attempts': 8, 'checks': [{'address': 0x900, 'bytes': [byte]}]}
                    (root/(tag+'.plan.json')).write_text(json.dumps(plan, indent=2)+'\n', encoding='utf-8')
                    result = experiment.run(plan, exe, root/tag)
                    check(result['ok'], 'context-bound experiment '+tag+': '+str(result.get('error', result['outcome'])))
                    records.append(result)
                comparison = experiment.compare(*records)
                check(comparison['metrics']['cycles']['baseline'] == '8' and comparison['metrics']['cycles']['candidate'] == '6', 'independent cycle counts 8 -> 6')
                check(comparison['metrics']['cycles']['reduction_percent'] == 25 and comparison['metrics']['image_bytes']['delta'] == -1, 'measured 25% cycle reduction and one byte saved')
                check(not comparison['bounds']['proven_floor'] and not comparison['bounds']['proven_ceiling'], 'observed improvement does not claim optimum')
                comparisons.append(comparison)
                for key, value in [('ok', False), ('platform', 99), ('workload_sha256', 'wrong'), ('executable_sha256', 'wrong')]:
                    bad = copy.deepcopy(records[1]); bad[key] = value
                    try:
                        experiment.compare(records[0], bad)
                    except ValueError:
                        pass
                    else:
                        raise AssertionError('incompatible result was compared: '+key)
                bad = copy.deepcopy(records[1]); bad['observations'][0]['bytes'][0] ^= 1
                try:
                    experiment.compare(records[0], bad)
                except ValueError:
                    check(True, 'wrong answer cannot win an efficiency comparison')
                else:
                    raise AssertionError('wrong answer accepted')
                if byte == 0:
                    wrong = copy.deepcopy(plan)
                    wrong['checks'][0]['bytes'][0] ^= 1
                    measured_failure = experiment.run(wrong, exe, root/f'p{platform}-wrong-result')
                    check(not measured_failure['ok'] and measured_failure['outcome'] == 'assertion-mismatch', 'actual wrong-result experiment fails and retains evidence')
                    invalid = copy.deepcopy(plan); invalid['attempts'] = True
                    try:
                        experiment.run(invalid, exe, root/f'p{platform}-invalid')
                    except ValueError:
                        check(not (root/f'p{platform}-invalid').exists(), 'invalid experiment refuses before creating output or launching')
                    else:
                        raise AssertionError('invalid experiment accepted')
        summary = {'schema': 'a2efficiency-study-v1', 'cases': comparisons, 'source': 'Rosetta asmiigs --format bin' if assembler else 'owned raw opcode fixtures',
                   'claim': 'controlled redundant-NOP removal demonstration across boundary inputs; no production-project gain or hardware optimum established'}
        (root/'study.json').write_text(json.dumps(summary, indent=2)+'\n', encoding='utf-8')
        return summary
    finally:
        if temporary:
            temporary.cleanup()


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--asmiigs', type=Path)
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    run(args.exe.resolve(), args.asmiigs.resolve() if args.asmiigs else None, args.out)
