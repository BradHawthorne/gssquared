"""Typed entry points to native machine context; no mutation retry or implicit run."""
from .observations import schema, number, integer
from .types import EXEC_PAUSED

READ_KINDS = ('overview', 'topology', 'device', 'address', 'irq', 'schedule', 'history', 'changes', 'stop', 'pixel')
TOOLS = [
    {'name': 'gs2_context_read',
     'description': 'Read synchronized native machine context, topology (including absent models), device fields, address routing, IRQ explanation, scheduler queues, retained causal history, or changes since a mark. Available while running: sampled on the machine thread without implicit pause. Separate calls may describe different boundaries. All model/coverage gaps remain in the result; read queries never execute the guest.',
     'inputSchema': schema({'kind': {'type': 'string', 'enum': list(READ_KINDS)},
                            'offset': number(0, 1000000), 'device_id': number(1, 255),
                            'address': number(0, 0xffffff), 'after_id': {'type': 'string', 'pattern': '^[0-9]+$'},
                            'count': number(1, 8), 'mark_id': {'type': 'string', 'pattern': '^[0-9]+$'},
                            'x': number(0, 639), 'y': number(0, 199)}, ('kind',))},
    {'name': 'gs2_context_track',
     'description': 'Change observer state only: start/stop bounded native context history or mark native CPU/device state plus an optional 1..256-byte CPU memory window for comparison. No guest execution. Mark retention is four; history capacity is at most 4096; loss is explicit.',
     'inputSchema': schema({'action': {'type': 'string', 'enum': ['on', 'off', 'mark']},
                            'capacity': number(1, 4096), 'address': number(0, 0xffffff),
                            'length': number(1, 256)}, ('action',))}
]
TOOL_NAMES = {row['name'] for row in TOOLS}


def identity(params, name, default=None):
    value = params.get(name, default)
    if not isinstance(value, str) or not value or not value.isascii() or not value.isdecimal() or len(value) > 20 or int(value) > 2**64-1:
        raise ValueError(name + ' must be an unsigned 64-bit decimal string')
    return str(int(value))


def call(client, name, params):
    if not isinstance(name, str) or name not in TOOL_NAMES or not isinstance(params, dict):
        raise ValueError('unknown context tool or invalid parameters')
    if name == 'gs2_context_read':
        kind = params.get('kind')
        allowed = {'overview': set(), 'topology': {'offset'}, 'device': {'device_id'},
                   'address': {'address'}, 'irq': set(), 'schedule': {'offset'},
                   'history': {'after_id', 'count'}, 'changes': {'mark_id', 'offset'},
                   'stop': set(), 'pixel': {'x', 'y'}}
        if not isinstance(kind, str) or kind not in allowed or set(params) - (allowed[kind] | {'kind'}):
            raise ValueError('unsupported context query parameters')
        command = 'context ' + kind
        if kind in ('topology', 'schedule'):
            command += ' ' + str(integer(params, 'offset', 0, 0, 1000000))
        elif kind == 'device':
            command += ' ' + str(integer(params, 'device_id', None, 1, 255))
        elif kind == 'address':
            command += ' ' + format(integer(params, 'address', None, 0, 0xffffff), '06X')
        elif kind == 'history':
            command += ' ' + identity(params, 'after_id', '0') + ' ' + str(integer(params, 'count', 4, 1, 8))
        elif kind == 'changes':
            command += ' ' + identity(params, 'mark_id') + ' ' + str(integer(params, 'offset', 0, 0, 1000000))
        elif kind == 'pixel':
            command += ' ' + str(integer(params, 'x', None, 0, 639)) + ' ' + str(integer(params, 'y', None, 0, 199))
    else:
        action = params.get('action')
        allowed = {'on': {'capacity'}, 'off': set(), 'mark': {'address', 'length'}}
        if not isinstance(action, str) or action not in allowed or set(params) - (allowed[action] | {'action'}):
            raise ValueError('unsupported context tracking parameters')
        command = 'context ' + action
        if action == 'on':
            command += ' ' + str(integer(params, 'capacity', 256, 1, 4096))
        elif action == 'mark' and ('address' in params or 'length' in params):
            address = integer(params, 'address', None, 0, 0xffffff)
            length = integer(params, 'length', None, 1, 256)
            if address + length > 0x1000000:
                raise ValueError('mark exceeds CPU memory space')
            command += ' ' + format(address, '06X') + ' ' + str(length)
    if name == 'gs2_context_track' and client.get_status().execution_mode != EXEC_PAUSED:
        raise ValueError('context tools require explicit pause and exclusive session ownership')
    reply = client.agent_command(command)
    value = reply.get('data')
    if not isinstance(value, dict) or value.get('schema') != 'a2context-v1':
        raise ValueError('selected emulator lacks native a2context-v1; discover live capabilities')
    return dict(value, ok=True)
