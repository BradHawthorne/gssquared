"""Artifact transport is bounded, content-bound and never overwrites input."""
import base64
import hashlib
import tempfile
from pathlib import Path

def test_artifact_roundtrip_ranges_and_create_once():
    from gs2debug.artifacts import Artifacts
    with tempfile.TemporaryDirectory() as temporary:
        store=Artifacts(Path(temporary));data=b'\x00\xa9\x42\xea';digest=hashlib.sha256(data).hexdigest()
        value,content=store.call('gs2_artifact_put',{'path':'fixture.bin','base64':base64.b64encode(data).decode(),'sha256':digest})
        assert value['sha256']==digest and value['created'] and not content
        for payload in ({'path':'fixture.bin','base64':base64.b64encode(data).decode(),'sha256':digest},{'path':'wrong.bin','text':'wrong','sha256':digest}):
            try:store.call('gs2_artifact_put',payload)
            except (ValueError,OSError):pass
            else:raise AssertionError('overwrite or hash mismatch accepted')
        assert not (Path(temporary)/'wrong.bin').exists()
        value,_=store.call('gs2_artifact_read',{'path':'fixture.bin','mode':'base64'})
        assert value['complete'] and value['sha256']==digest and base64.b64decode(value['base64'])==data
        value,_=store.call('gs2_artifact_read',{'path':'fixture.bin','offset':1,'length':2,'mode':'base64'})
        assert not value['complete'] and value['sha256']==hashlib.sha256(data[1:3]).hexdigest() and value['offset']==1
        listing,_=store.call('gs2_artifact_list',{'limit':1})
        assert listing['entries'][0]['path']=='fixture.bin' and not listing['truncated']

def test_artifact_scope_limits_and_missing_paths():
    from gs2debug.artifacts import Artifacts,MAX_READ
    with tempfile.TemporaryDirectory() as temporary:
        root=Path(temporary);store=Artifacts(root);(root/'large.bin').write_bytes(b'x'*(MAX_READ+1))
        (root/'a.txt').write_text('a');(root/'b.txt').write_text('b')
        value,_=store.call('gs2_artifact_list',{'limit':1})
        assert value['truncated'] and len(value['entries'])==1
        for name,args in [('gs2_artifact_read',{'path':'../escape'}),('gs2_artifact_read',{'path':'absent'}),('gs2_artifact_read',{'path':'large.bin'}),('gs2_artifact_read',{'path':'a.txt','length':True}),('gs2_artifact_put',{'path':'../escape','text':'x','sha256':'0'*64}),('gs2_artifact_read',{'path':'a.txt','sha256':'0'*64})]:
            try:store.call(name,args)
            except (ValueError,OSError):pass
            else:raise AssertionError('invalid artifact request accepted')
        try:
            (root/'link').symlink_to(root.parent,target_is_directory=True)
        except OSError:pass  # Windows may prohibit creation; traversal is tested above.
        else:
            try:store.call('gs2_artifact_read',{'path':'link/outside'})
            except (ValueError,OSError):pass
            else:raise AssertionError('symlink escape accepted')

def test_text_and_corrupt_image_are_not_plausible_success():
    from gs2debug.artifacts import Artifacts
    with tempfile.TemporaryDirectory() as temporary:
        root=Path(temporary);store=Artifacts(root);(root/'unicode.txt').write_bytes('café\n'.encode('utf-8'))
        value,content=store.call('gs2_artifact_read',{'path':'unicode.txt'})
        assert value['text']=='café\n' and value['complete'] and not content
        (root/'bad.png').write_bytes(b'\x89PNG\r\n\x1a\nnot-a-png')
        try:store.call('gs2_artifact_read',{'path':'bad.png'})
        except ValueError:pass
        else:raise AssertionError('corrupt image advertised as viewable evidence')
