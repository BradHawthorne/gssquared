"""Transport failures must not cause implicit mutation retries or framing loss."""
import json
from pathlib import Path
import socket
import tempfile
import threading
import time
from gs2debug import Client,CtrlClient
from gs2debug.ctrl import ControlError
from gs2debug.frame import pack_frame,unpack_header
from gs2debug.types import AGENT_COMMAND,PING,EVENT

def test_file_client_rejects_controls_before_publishing():
    with tempfile.TemporaryDirectory() as temporary:
        with CtrlClient(temporary,timeout=.01) as client:
            for command in ('cpu\nreset','cpu\rreset','cpu\treset','cpu\x7f','cpu\x00','cpu\x1b'):
                try:client.command(command)
                except ControlError:pass
                else:raise AssertionError('control character accepted')
                assert list(Path(temporary).glob('cmd.*'))==[],'invalid command was published'

def peer_request(peer):
    raw=b''
    while len(raw)<12:raw+=peer.recv(12-len(raw))
    kind,sequence,length=unpack_header(raw);payload=b''
    while len(payload)<length:payload+=peer.recv(length-len(payload))
    return kind,sequence,payload

def test_partial_reply_timeout_recovers_without_resending():
    for split in (5,18):  # midway through header, then through payload
        client=Client();local,remote=socket.socketpair();client._sock=local;client._handshaked=True
        release=threading.Event();received=[];errors=[]
        def peer():
            try:
                kind,sequence,payload=peer_request(remote);received.append((kind,sequence,payload))
                reply=json.dumps({'schema':'a2ctrl-reply-v2','ok':True,'command':payload.decode(),'data':{'effect':'once'}}).encode()
                wire=pack_frame(kind,sequence,reply);remote.sendall(wire[:split])
                if not release.wait(3):raise AssertionError('test did not release reply')
                remote.sendall(wire[split:])
                kind,sequence,payload=peer_request(remote);received.append((kind,sequence,payload));remote.sendall(pack_frame(kind,sequence,b''))
            except BaseException as error:errors.append(error)
        worker=threading.Thread(target=peer,daemon=True);worker.start()
        try:
            try:client.agent_command('setreg a 42',timeout=.03)
            except TimeoutError:pass
            else:raise AssertionError('partial reply did not time out')
            pending=client.pending_request
            assert pending['type']==AGENT_COMMAND and pending['sequence']==1
            try:client.agent_command('setreg a 99')
            except TimeoutError:pass
            else:raise AssertionError('new mutation allowed before original result was recovered')
            assert client._next_seq==2
            release.set();result=client.wait_agent_command(timeout=1)
            assert result['command']=='setreg a 42' and result['data']['effect']=='once' and client.pending_request is None
            client.ping();worker.join(1)
            assert not worker.is_alive() and not errors and [row[0] for row in received]==[AGENT_COMMAND,PING]
        finally:release.set();client.close();remote.close();worker.join(1)

def test_partial_event_timeout_preserves_framing():
    client=Client();local,remote=socket.socketpair();client._sock=local;client._handshaked=True
    wire=pack_frame(EVENT,11,(99).to_bytes(4,'little')+b'evidence')
    try:
        remote.sendall(wire[:7])
        try:client.wait_event(timeout=.02)
        except TimeoutError:pass
        else:raise AssertionError('incomplete event did not time out')
        remote.sendall(wire[7:])
        assert client.wait_event(timeout=1)==(99,11,b'evidence')
    finally:client.close();remote.close()

def test_bad_timeout_has_no_wire_effect():
    client=Client();local,remote=socket.socketpair();client._sock=local;client._handshaked=True
    try:
        for timeout in (True,0,-1,float('nan'),float('inf')):
            try:client.request(PING,timeout=timeout)
            except ValueError:pass
            else:raise AssertionError('invalid timeout accepted')
        remote.settimeout(.02)
        try:data=remote.recv(1)
        except TimeoutError:pass
        else:raise AssertionError('invalid request emitted bytes: '+repr(data))
    finally:client.close();remote.close()

def test_unrelated_events_do_not_restart_stop_deadline():
    client=Client();calls=[]
    def unrelated(*,timeout):
        calls.append(timeout)
        if len(calls)>20:raise AssertionError('unrelated events restarted the wait budget')
        time.sleep(.002);return (99,1,b'')
    client.wait_event=unrelated
    try:client.wait_stopped(timeout=.01)
    except TimeoutError:pass
    else:raise AssertionError('stop wait did not expire')
    assert len(calls)<20
    if len(calls)>1:assert calls[-1]<calls[0]
