"""Close-out evidence remains honest when a transport result is unavailable."""
import json
from pathlib import Path
import socket
import subprocess
import tempfile
from types import SimpleNamespace

from gs2debug import Client,RequestPending
from gs2debug.mcp import Backend
from gs2debug.types import QUIT


class PendingClient:
    pending={'sequence':7,'type':0x1234,'effect':'unknown until original response; timeout is not cancellation'}
    def __init__(self):self.closed=False
    @property
    def pending_request(self):return None if self.closed else dict(self.pending)
    def quit(self,**kwargs):raise AssertionError('pending command must not be retried or replaced by QUIT')
    def close(self):self.closed=True


class OwnedChild:
    pid=12345
    def __init__(self,exited=False):self.returncode=-9 if exited else None;self.kills=0;self.waits=[]
    def poll(self):return self.returncode
    def wait(self,timeout):
        self.waits.append(timeout)
        if self.returncode is None:raise subprocess.TimeoutExpired('owned-fixture',timeout)
        return self.returncode
    def kill(self):self.kills+=1;self.returncode=-9


def test_quit_reply_wait_is_bounded_and_never_reissued():
    client=Client();left,right=socket.socketpair();client._sock=left;client._handshaked=True
    try:
        try:client.quit(timeout=.01)
        except RequestPending:pass
        else:raise AssertionError('missing QUIT reply reported success')
        assert client.pending_request['type']==QUIT
        sent=right.recv(4096)
        try:client.quit(timeout=.01)
        except RequestPending:pass
        else:raise AssertionError('second request sent while first is pending')
        right.settimeout(.02)
        try:right.recv(4096)
        except TimeoutError:pass
        else:raise AssertionError('QUIT was sent twice')
        assert sent
    finally:client.close();right.close()


def test_pending_close_reaps_its_owned_child_and_keeps_unknown_effects():
    with tempfile.TemporaryDirectory(prefix='mcp-close-unit-') as directory:
        backend=Backend(SimpleNamespace(exe=Path('fixture.exe'),read_only_media=False))
        backend.root=Path(directory);backend.client=PendingClient();backend.process=OwnedChild()
        backend.close()
        owner=json.loads((backend.root/'mcp-session.json').read_text())
        assert backend.process.kills==1 and owner['closed'] and owner['exit_code']==-9
        assert owner['shutdown']['effects_unknown'] and owner['shutdown']['pending_request']==PendingClient.pending
        assert owner['shutdown']['forced_termination'] and not owner['shutdown']['quit_acknowledged']
        before=(backend.root/'mcp-session.json').read_bytes();backend.close()
        assert backend.process.kills==1 and (backend.root/'mcp-session.json').read_bytes()==before


def test_attached_close_disconnects_without_quit_or_a_process():
    backend=Backend(SimpleNamespace(exe=None));backend.client=PendingClient()
    backend.close()
    assert backend.client.closed and backend.process is None
    assert backend.shutdown['outcome']=='attached-disconnected' and backend.shutdown['effects_unknown']
    assert not backend.shutdown['forced_termination'] and not backend.shutdown['quit_acknowledged']


def test_already_exited_child_does_not_erase_pending_outcome():
    with tempfile.TemporaryDirectory(prefix='mcp-exited-unit-') as directory:
        backend=Backend(SimpleNamespace(exe=Path('fixture.exe'),read_only_media=False))
        backend.root=Path(directory);backend.client=PendingClient();backend.process=OwnedChild(exited=True)
        backend.close()
        owner=json.loads((backend.root/'mcp-session.json').read_text())
        assert backend.process.kills==0 and owner['closed'] and owner['exit_code']==-9
        assert owner['shutdown']['effects_unknown'] and not owner['shutdown']['quit_acknowledged']


def test_unanswered_closing_quit_is_bounded_and_retained():
    class NoReply(PendingClient):
        def __init__(self):super().__init__();self.sent=False
        @property
        def pending_request(self):
            return {'sequence':8,'type':QUIT,'effect':'unknown until original response; timeout is not cancellation'} if self.sent and not self.closed else None
        def quit(self,*,timeout):
            assert 0<timeout<=2
            self.sent=True
            raise RequestPending(8,QUIT)
    with tempfile.TemporaryDirectory(prefix='mcp-quit-unit-') as directory:
        backend=Backend(SimpleNamespace(exe=Path('fixture.exe'),read_only_media=False))
        backend.root=Path(directory);backend.client=NoReply();backend.process=OwnedChild()
        backend.close()
        owner=json.loads((backend.root/'mcp-session.json').read_text())
        assert owner['closed'] and backend.process.kills==1
        assert owner['shutdown']['effects_unknown'] and owner['shutdown']['pending_request']['type']==QUIT
        assert owner['shutdown']['quit_attempted'] and not owner['shutdown']['quit_acknowledged']
        assert len(owner['shutdown']['errors'])==1
