"""Machine-context evidence, including deliberate refusals, on IIe and IIgs."""
import argparse
import json
from pathlib import Path
import sys
import subprocess
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parent))
from p0check import Session, check, environment


def run(exe):
    for platform in (3, 5):
        session = Session(exe, platform)
        try:
            def call(arguments, ok=True):
                reply = json.loads(session.cmd('json context ' + arguments, ok=False))
                check(reply['ok'] == ok, 'context verdict ' + arguments)
                return reply['data']

            overview = call('overview')
            check(overview['schema'] == 'a2context-v1', 'native context schema')
            check(not overview['data']['history']['active'], 'history is explicitly off by default')
            initial_cycles = overview['stamp']['cpu_cycles']
            if platform == 3:
                check(call('pixel 0 0')['data']['video_flags'] >= 0, 'pixel query reads current-machine scanner before the first frame')
            address = call('address 000800')['data']
            check(address['readable'] and address['value'] is not None, 'RAM explains actual probe value')
            io = call('address 00C030')['data']
            check(not io['readable'] and io['value'] is None, 'I/O explanation never returns placeholder as truth')
            check(call('overview')['stamp']['cpu_cycles'] == initial_cycles, 'observation consumes no guest cycles')
            nodes = []
            offset = 0
            while True:
                topology = call('topology ' + str(offset))['data']
                nodes.extend(topology['nodes'])
                if topology['next_offset'] is None:
                    break
                offset = topology['next_offset']
            check(any(node.get('installed') is False for node in nodes), 'absent models are discoverable')
            absent = next(node for node in nodes if node.get('installed') is False and 'device_id' in node)
            check(call('device '+str(absent['device_id']))['data']['observers'] == [], 'absent model cannot borrow another installed model observer')
            check(any(node['id'] == 'cpu' for node in nodes), 'CPU participates in topology')
            check(all(node.get('source') for node in nodes), 'topology links implementation sources')
            call('on 0', False)
            check(not call('overview')['data']['history']['active'], 'invalid arm does not change history')
            call('on 128')
            call('on 128', False)
            mark = call('mark 000900 1')['data']['mark_id']
            session.cmd('poke 000900 11')
            session.cmd('poke 000800 A9 42 8D 00 09 EA')
            session.cmd('setreg pc 000800')
            session.cmd('step 3')
            changes = []; offset = 0
            while True:
                page = call('changes ' + mark + ' ' + str(offset))['data']
                changes.extend(page['changes'])
                if page['next_offset'] is None:
                    break
                offset = page['next_offset']
            memory = next(row for row in changes if row['field'] == 'memory.000900')
            check(memory['after']['value'] == 0x42, 'instruction store joins marked memory delta')
            check(any(row['field'] == 'cpu.a' and row['after']['value'] == 0x42 for row in changes), 'CPU register delta is typed')
            call('changes 999999', False)
            call('address 1000000', False)
            call('history 999999 4', False)
            session.cmd('fault schedule 0 irq-on')
            session.cmd('step 1', ok=False)
            irq = call('irq')['data']
            check(any(row['id'] == 14 and row['asserted'] for row in irq['sources']), 'IRQ explains independent asserted source')
            session.cmd('fault clear')
            session.cmd('fault schedule 0 nmi')
            session.cmd('step 1', ok=False)
            session.cmd('fault clear', ok=False)  # Delivered one-shot has no active source.
            events = []; cursor = '0'
            while True:
                page = call('history ' + cursor + ' 8')['data']
                events.extend(page['events'])
                if not page['has_more']:
                    break
                cursor = page['next_cursor']
            check(any(row['kind'] == 'interrupt' and row['payload']['kind'] == 'NMI' for row in events), 'accepted NMI has exact transition evidence')
            accepted = {row['id'] for row in events if row['kind'] == 'interrupt' and row['payload']['phase'] == 'accepted-before-stack-and-vector-access'}
            check(any(row['kind'] == 'interrupt' and row['payload']['phase'] == 'vector-resolved' and row['parent_id'] in accepted for row in events), 'resolved vector links its actual interrupt entry')
            check(any(row['kind'] == 'irq' for row in events), 'IRQ edge retained without separate subscription')
            check(call('stop')['data']['last_retained_execution'] is not None, 'stop explanation links execution receipt')
            session.cmd('run-fake', ok=False)
            receipt = call('stop')['data']['last_retained_execution']['receipt']
            check(receipt['data']['schema'] == 'a2execution-v1', 'stop explanation ignores similarly named non-execution commands')
            check(all(row['stamp']['cpu_cycles'] is not None for row in events), 'event clocks are explicit')
            begins = {row['id'] for row in events if row['kind'] == 'command.begin'}
            check(any(row['kind'] == 'command' and row['parent_id'] in begins for row in events), 'command completion refers to its actual begin event')
            timers = call('schedule')['data']
            check({row['domain'] for row in timers['queues']} == {'master-14m', 'video', 'cpu'}, 'scheduler clocks are separate domains')
            snapshot = session.dir / 'context snapshot.json'
            ref = call('snapshot "' + snapshot.as_posix() + '"')
            full = json.loads(snapshot.read_text(encoding='utf-8'))
            check(ref['stamp'] == full['stamp'], 'full snapshot and reference have identical observation identity')
            fingerprint = 1469598103934665603
            for byte in snapshot.read_bytes():
                fingerprint = ((fingerprint ^ byte) * 1099511628211) & (2**64-1)
            check(ref['producer_finalized'] and int(ref['bytes']) == snapshot.stat().st_size and ref['hash'] == str(fingerprint), 'producer receipt binds closed snapshot bytes')
            check(full['data']['devices'] and 'signals' in full['data'], 'snapshot contains native device and signal observations')
            check(full['capture_host_ns'].isdigit(), 'capture overhead is measured in host time')
            pixel = call('pixel 0 0')['data']
            check('reference' in pixel['evidence_class'] and pixel['source'], 'pixel explanation names its model and implementation')
            call('pixel 640 0', False)
            if platform == 5:
                session.cmd('setreg e 1')
                session.cmd('setreg p 34')
                session.cmd('poke 000800 A9 81 8D 29 C0')
                session.cmd('setreg pc 000800')
                session.cmd('step 2')
                session.cmd('poke E19D00 00')
                session.cmd('poke E12000 12')
                session.cmd('poke E19E02 23 01')
                pixel = call('pixel 0 0')['data']
                check(pixel['pixel_byte'] == 0x12 and pixel['palette_index'] == 1 and pixel['rgb'] == [17, 34, 51], 'SHR explanation follows physical interleaving and RGB nibbles')
                session.cmd('poke E19D00 20')
                check(call('pixel 0 0')['data']['rgb'] is None, 'unreconstructed fill history refuses a false RGB')
                session.cmd('poke E19D00 80')
                session.cmd('poke E12000 40')
                session.cmd('poke E19E12 56 04')
                pixel = call('pixel 0 0')['data']
                check(pixel['palette_index'] == 9 and pixel['rgb'] == [68, 85, 102], '640-mode split palette group is explicit')
                session.cmd('poke 000800 A9 00 8D 05 C0 A9 55 8D 00 09 A9 00 8D 03 C0')
                session.cmd('setreg pc 000800')
                session.cmd('step 2')  # RAMWRT on; RAMRD remains off.
                mapping = call('address 000900')['data']
                check(mapping['resolved_read']['offset'] == 0x900 and mapping['resolved_write']['offset'] == 0x10900, 'address explanation separates actual read/write mappings')
                session.cmd('step 2')  # Store follows write mapping.
                check(call('address 000900')['data']['value'] == 0x42, 'main read stays unchanged after aux write')
                session.cmd('step 2')  # RAMRD on: same logical address now reads aux.
                check(call('address 000900')['data']['value'] == 0x55, 'observed CPU mapping confirms explained aux destination')
            call('off')
            call('on 4')
            for _ in range(6):
                session.cmd('cpu')
            lost = call('history 0 8')['data']
            check(lost['lost_before_cursor'] != '0' and not lost['complete'], 'overwritten history reports loss')
            call('off')
            call('off', False)
        finally:
            session.close()
        mcp(exe, platform)
        witnesses = []
        for armed in (False, True):
            fixture = Session(exe, platform)
            try:
                fixture.code([0xad, 0x30, 0xc0, 0xa9, 0x42, 0x8d, 0, 9, 0xea])
                if armed:
                    fixture.cmd('context on 256')
                result = json.loads(fixture.cmd('json step 4'))['data']
                state = json.loads(fixture.cmd('json context overview'))['data']
                witnesses.append((result, state['stamp']['cpu_cycles'], state['stamp']['master_14m_ticks']))
            finally:
                fixture.close()
        check(witnesses[0] == witnesses[1], 'armed context preserves actual instruction/CPU/cycle witnesses on platform '+str(platform))


def mcp(exe, platform):
    with tempfile.TemporaryDirectory(prefix='a2context-mcp-') as temporary:
        root = Path(temporary)
        requests = []
        def request(method, params):
            identity = len(requests) + 1
            requests.append({'jsonrpc': '2.0', 'id': identity, 'method': method, 'params': params})
            return identity
        def tool(name, arguments):
            return request('tools/call', {'name': 'gs2_' + name, 'arguments': arguments})
        request('initialize', {'protocolVersion': '2025-11-25', 'capabilities': {}, 'clientInfo': {'name': 'context-fixture', 'version': '1'}})
        requests.append({'jsonrpc': '2.0', 'method': 'notifications/initialized'})
        listing = request('tools/list', {})
        tracking = tool('context_track', {'action': 'on', 'capacity': 128})
        before = tool('context_read', {'kind': 'overview'})
        tool('resume', {})
        live = tool('context_read', {'kind': 'overview'})
        running_mutation = tool('context_track', {'action': 'mark'})
        tool('pause', {})
        current = tool('context_read', {'kind': 'overview'})
        invalid = tool('context_read', {'kind': 'address', 'address': True})
        tool('context_track', {'action': 'off'})
        process = subprocess.run([sys.executable, '-I', str(Path(__file__).with_name('mcpserver.py')),
                                  '--exe', str(exe), '--platform', str(platform), '--out-dir', str(root/'machine')],
                                 input=('\n'.join(map(json.dumps, requests))+'\n').encode(), capture_output=True,
                                 env=environment(), timeout=60)
        check(process.returncode == 0, 'context MCP lifecycle: ' + process.stderr.decode(errors='replace')[-300:])
        replies = {row['id']: row for row in map(json.loads, process.stdout.splitlines())}
        names = {row['name'] for row in replies[listing]['result']['tools']}
        check({'gs2_context', 'gs2_context_read', 'gs2_context_track'} <= names, 'native and typed context tools discoverable')
        def value(identity):
            result = replies[identity]['result']
            check(not result['isError'], 'typed context result ' + str(identity))
            return result['structuredContent']
        check(value(tracking)['data']['active'], 'MCP arms native history')
        check(value(live)['data']['cpu'] and value(live)['data']['history']['active'], 'live context reads while machine runs')
        check(int(value(current)['stamp']['cpu_cycles']) >= int(value(before)['stamp']['cpu_cycles']), 'live context carries native monotonic counters')
        check(replies[running_mutation]['result']['isError'] and replies[invalid]['result']['isError'], 'observer mutation and malformed query refuse')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe', type=Path, required=True)
    run(parser.parse_args().exe.resolve())
