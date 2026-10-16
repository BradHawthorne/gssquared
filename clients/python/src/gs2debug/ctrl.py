"""Typed file-rail client. One lease, one pending command, atomic publication.

Timeout leaves an owned pending command: call wait(), never issue it again.
Artifacts are retained. A lease is released only by its owning client; stale
leases require explicit operator cleanup. Do not mix writers on one session.
"""
import json
import math
import os
from pathlib import Path
import re
import time
import uuid


class ControlError(RuntimeError):
    pass


class CommandPending(ControlError):
    pass


class CommandFailed(ControlError):
    def __init__(self,reply):
        self.reply=reply
        super().__init__(f"{reply['command']}: {reply.get('status','FAIL')}: {reply.get('text','')}")


def quoted(value):
    value=str(value)
    if any(c in value for c in ('"','\0','\n','\r')):raise ControlError('unsupported quoted argument')
    return '"'+value+'"'


class CtrlClient:
    """Attach to a chosen file-rail session; retain native observation artifacts.

    command() accepts unwrapped CTRL commands and returns a2ctrl-reply-v2 JSON.
    The optional owner must match session.json. close() releases this writer's
    lease; ending the emulator requires an explicit command('quit').
    """
    def __init__(self,directory,*,owner=None,timeout=20):
        self.directory=Path(directory).resolve()
        if not self.directory.is_dir():raise ControlError('control directory does not exist')
        if type(timeout) not in (int,float) or not math.isfinite(timeout) or not 0<timeout<=3600:
            raise ControlError('invalid timeout')
        metadata=self.directory/'session.json'
        if metadata.exists():
            try:session=json.loads(metadata.read_text(encoding='utf-8-sig'))
            except (OSError,ValueError) as exc:raise ControlError('invalid session metadata') from exc
            if not isinstance(session,dict):raise ControlError('invalid session metadata')
            if owner is not None and session.get('owner')!=owner:raise ControlError('session owner mismatch')
        elif owner is not None:raise ControlError('owner cannot be verified without session.json')
        self.timeout=timeout;self.pending=None;self.closed=False
        self.token=uuid.uuid4().hex;self.lease=self.directory/'.ctrl-client.lock'
        try:
            descriptor=os.open(self.lease,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600)
        except FileExistsError as exc:raise ControlError('session already has a CtrlClient lease') from exc
        self.lease_text=json.dumps({'schema':'a2ctrl-lease-v1','pid':os.getpid(),'token':self.token})
        with os.fdopen(descriptor,'w',encoding='utf-8') as stream:stream.write(self.lease_text)

    def __enter__(self):return self

    def __exit__(self,*ignored):self.close()

    def close(self):
        if not self.closed:
            if self.lease.exists() and self.lease.read_text(encoding='utf-8')==self.lease_text:self.lease.unlink()
            self.closed=True

    def command(self,command,*,check=True):
        """Issue once and wait; CommandPending means wait(), not reissue.

        With check=True, a negative native verdict raises CommandFailed and
        retains the full reply on the exception. HALT is a successful stop.
        """
        if self.closed:raise ControlError('client is closed')
        if self.pending is not None:raise CommandPending('a command is pending; call wait()')
        if not isinstance(command,str) or not command or any(ord(c)<32 or ord(c)==127 for c in command) or command.startswith('json '):
            raise ControlError('expected an unwrapped CTRL command')
        try:encoded=('json '+command+'\n').encode('utf-8')
        except UnicodeError as error:raise ControlError('command requires valid UTF-8') from error
        if len(encoded)>2047:raise ControlError('command exceeds the CTRL frame limit')
        sequences=[int(m[1]) for path in self.directory.iterdir() if (m:=re.fullmatch(r'cmd\.(\d+)',path.name))]
        previous=max(sequences,default=0)
        if previous and not (self.directory/f'ack.{previous}').exists():
            raise CommandPending('another writer has an unacknowledged command; nothing issued')
        sequence=previous+1
        temporary=self.directory/('.pending-'+self.token)
        created=False
        try:
            with temporary.open('xb') as stream:stream.write(encoded)
            created=True
            # A new directory entry atomically names the already-closed file.
            # Unlike replace(), link() can never overwrite another writer's cmd.N.
            os.link(temporary,self.directory/f'cmd.{sequence}')
            self.pending=(sequence,command)
        except OSError as exc:raise ControlError('cannot publish command atomically: '+str(exc)) from exc
        finally:
            if created and temporary.exists():temporary.unlink()
        return self.wait(check=check)

    def wait(self,*,check=True):
        """Resume waiting for the owned pending sequence without issuing I/O."""
        if self.closed:raise ControlError('client is closed')
        if self.pending is None:raise ControlError('no owned pending command')
        sequence,command=self.pending;path=self.directory/f'ack.{sequence}'
        deadline=time.monotonic()+self.timeout
        while time.monotonic()<deadline:
            try:
                content=path.read_text(encoding='utf-8')
            except (FileNotFoundError,PermissionError):time.sleep(.01);continue
            try:reply=json.loads(content)
            except ValueError as exc:raise ControlError('ack is not valid JSON') from exc
            if (not isinstance(reply,dict) or reply.get('schema')!='a2ctrl-reply-v2' or
                type(reply.get('ok')) is not bool or reply.get('command')!=command):
                raise ControlError('ack schema/command mismatch')
            self.pending=None
            if check and not reply['ok']:raise CommandFailed(reply)
            return reply
        raise CommandPending(f'command {sequence} issued but pending; call wait()')

    def inspect(self,kind,*arguments):
        """Return checked a2inspect-v1 JSON, retaining its file in the session.

        Kinds: cpu, devices, capabilities, signals [glob], memory HEX_ADDR LEN.
        """
        if kind not in ('cpu','devices','signals','capabilities','memory'):raise ControlError('unknown inspection kind')
        target=self.directory/('inspect-'+uuid.uuid4().hex+'.json')
        command='inspect '+kind+' '+quoted(target)+''.join(' '+quoted(arg) for arg in arguments)
        reply=self.command(command)
        ref=reply.get('data')
        if (not isinstance(ref,dict) or ref.get('schema')!='a2inspect-ref-v1' or not isinstance(ref.get('path'),str)
            or Path(ref['path']).resolve()!=target):
            raise ControlError('inspection artifact reference mismatch')
        artifact=json.loads(target.read_text(encoding='utf-8'))
        if not isinstance(artifact,dict) or artifact.get('schema')!='a2inspect-v1' or artifact.get('kind')!=kind:raise ControlError('inspection artifact schema mismatch')
        return artifact

    def cpu(self):
        data=self.command('cpu').get('data')
        if not isinstance(data,dict) or data.get('schema')!='a2cpu-v1':raise ControlError('CPU reply lacks native state')
        if not isinstance(data.get('state'),dict):raise ControlError('invalid native CPU state')
        return data['state']
