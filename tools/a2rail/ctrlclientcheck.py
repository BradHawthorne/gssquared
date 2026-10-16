"""File-client lease, framing and pending-command negative controls."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'clients/python/src'))
from gs2debug import CtrlClient, ControlError, CommandPending, CommandFailed

class FileClientTests(unittest.TestCase):
    def test_lease_and_atomic_frame(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            with CtrlClient(root,timeout=.02) as client:
                with self.assertRaisesRegex(ControlError,'lease'):CtrlClient(root)
                with self.assertRaises(CommandPending):client.command('cpu')
                self.assertEqual((root/'cmd.1').read_text(),'json cpu\n')
                self.assertFalse(list(root.glob('.pending-*')))
                with self.assertRaises(CommandPending):client.command('step 1')
                self.assertFalse((root/'cmd.2').exists())
                reply={'schema':'a2ctrl-reply-v2','ok':True,'command':'cpu','data':{'native':1}}
                (root/'ack.1').write_text(json.dumps(reply))
                self.assertEqual(client.wait(),reply)
            self.assertFalse((root/'.ctrl-client.lock').exists())

    def test_wrong_owner_and_wrong_ack(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);(root/'session.json').write_text(json.dumps({'owner':'owner'}))
            with self.assertRaisesRegex(ControlError,'owner'):CtrlClient(root,owner='wrong')
            self.assertFalse((root/'.ctrl-client.lock').exists())
            with CtrlClient(root,owner='owner',timeout=.02) as client:
                with self.assertRaises(CommandPending):client.command('cpu')
                (root/'ack.1').write_text(json.dumps({'schema':'a2ctrl-reply-v2','ok':True,'command':'step 1'}))
                with self.assertRaisesRegex(ControlError,'mismatch'):client.wait()
                self.assertIsNotNone(client.pending)

    def test_failed_reply_and_preexisting_pending(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            with CtrlClient(root,timeout=.02) as client:
                with self.assertRaises(CommandPending):client.command('invalid')
                (root/'ack.1').write_text(json.dumps({'schema':'a2ctrl-reply-v2','ok':False,'status':'FAIL','command':'invalid'}))
                with self.assertRaises(CommandFailed):client.wait()
                self.assertIsNone(client.pending)
            (root/'cmd.2').write_text('run 100\n')
            with CtrlClient(root,timeout=.02) as client:
                with self.assertRaises(CommandPending):client.command('cpu')
                self.assertFalse((root/'cmd.3').exists())

if __name__=='__main__':unittest.main(verbosity=2)
