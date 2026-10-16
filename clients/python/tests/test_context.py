from types import SimpleNamespace
from gs2debug.context import call


class Machine:
    def __init__(self, running=False):
        self.running = running
        self.commands = []

    def get_status(self):
        return SimpleNamespace(execution_mode=0 if self.running else 2)

    def agent_command(self, command):
        self.commands.append(command)
        return {'data': {'schema': 'a2context-v1', 'kind': command.split()[1]}}


def test_context_has_typed_parameters_and_live_reads():
    machine = Machine(running=True)
    assert call(machine, 'gs2_context_read', {'kind': 'address', 'address': 0xc030})['ok']
    assert machine.commands == ['context address 00C030']
    try:
        call(machine, 'gs2_context_track', {'action': 'on'})
    except ValueError:
        pass
    else:
        raise AssertionError('running observer mutation accepted')
    assert len(machine.commands) == 1


def test_context_refuses_bad_inputs_before_sending():
    machine = Machine()
    cases = [('gs2_context_read', {'kind': 'address', 'address': True}),
             ('gs2_context_read', {'kind': 'irq', 'offset': 1}),
             ('gs2_context_read', {'kind': 'history', 'after_id': '1\nreset'}),
             ('gs2_context_read', {'kind': 'history', 'after_id': str(2**64)}),
             ('gs2_context_read', {'kind': 'changes', 'mark_id': '1', 'offset': -1}),
             ('gs2_context_track', {'action': 'on', 'capacity': 0}),
             ('gs2_context_track', {'action': 'mark', 'address': 0xffffff, 'length': 2}),
             ('gs2_context_track', {'action': 'off', 'capacity': 8})]
    cases += [('gs2_context_read', {'kind': []}), ('gs2_context_track', {'action': []})]
    for name, params in cases:
        try:
            call(machine, name, params)
        except ValueError:
            pass
        else:
            raise AssertionError('invalid context parameters accepted')
    assert not machine.commands


def test_context_mark_and_changes_keep_u64_identity():
    machine = Machine()
    call(machine, 'gs2_context_track', {'action': 'mark', 'address': 0x14000, 'length': 4})
    call(machine, 'gs2_context_read', {'kind': 'changes', 'mark_id': str(2**63+7)})
    assert machine.commands == ['context mark 014000 4', 'context changes 9223372036854775815 0']
