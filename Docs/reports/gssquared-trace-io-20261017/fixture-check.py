from pathlib import Path
import hashlib
import json
import subprocess


def main():
    root = Path('D:/gsqs/traceio-20261017')
    source = root / 'src'
    baseline = '0fbd0baff8ef4174cf4353940a793d82167f6853'
    old = root / 'baseline-includes'
    (old / 'debugger').mkdir(parents=True, exist_ok=True)
    identities = {}
    for name in ('debugger/trace.cpp', 'debugger/trace.hpp'):
        body = subprocess.run(['git', '-C', str(source), 'show', baseline + ':src/' + name],
                              capture_output=True, check=True).stdout
        (old / name).write_bytes(body)
        identities[name] = hashlib.sha256(body).hexdigest()
    fixture = source / 'Docs/reports/gssquared-trace-io-20261017/trace_fixture.cpp'
    rows = []
    commands = {}
    for variant in ('baseline', 'candidate'):
        exe = root / (variant + '-fixture.exe')
        implementation = old / 'debugger/trace.cpp' if variant == 'baseline' else source / 'src/debugger/trace.cpp'
        includes = ['-I', str(old)] if variant == 'baseline' else []
        command = ['g++', '-std=c++17', *includes, '-I', str(source / 'src'), str(fixture),
                   str(implementation), str(source / 'src/debugger/trace_opcodes.cpp'), '-o', str(exe)]
        commands[variant] = command
        compiled = subprocess.run(command, capture_output=True, timeout=60)
        (root / (variant + '-fixture-build.log')).write_bytes(compiled.stdout + compiled.stderr)
        assert compiled.returncode == 0, compiled.stderr
        for mode in ('binary', 'blocked'):
            destination = root / (variant + ('-blocked' if mode == 'blocked' else '.bin'))
            result = subprocess.run([str(exe), str(destination), mode], capture_output=True, timeout=10)
            output = result.stdout + result.stderr
            (root / (variant + '-' + mode + '.log')).write_bytes(output)
            expected = 2 if variant == 'candidate' and mode == 'blocked' else 0
            assert result.returncode == expected, (variant, mode, result.returncode, output)
            if mode == 'binary':
                assert (b'exact_binary=0' if variant == 'baseline' else b'exact_binary=1') in output, output
            elif variant == 'baseline':
                assert b'save returned success' in output, output
            else:
                assert b'refused: open' in output and b'save returned success' not in output, output
            rows.append({'variant': variant, 'mode': mode, 'exit_code': result.returncode,
                         'expected_exit': expected, 'expectation_met': True})
            print('PASS real-stdio', variant, mode, flush=True)
    record = {'baseline_commit': baseline, 'baseline_source_sha256': identities,
              'fixture_sha256': hashlib.sha256(fixture.read_bytes()).hexdigest(),
              'compile_argv': commands, 'checks': rows,
              'scope': 'same fixture, real Windows stdio; candidate exception maps to exit 2 in fixture; application boundary inspected'}
    (root / 'fixture-checks.json').write_bytes((json.dumps(record, indent=2) + '\n').encode())


if __name__ == '__main__':
    main()
