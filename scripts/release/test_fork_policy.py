"""Fork publication policy fixtures. No SSH, fetch, upload or package build."""
import contextlib
import importlib.machinery
import importlib.util
import io
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

loader = importlib.machinery.SourceFileLoader('gs2_release', str(Path(__file__).with_name('gs2-release')))
spec = importlib.util.spec_from_loader(loader.name, loader)
release = importlib.util.module_from_spec(spec)
sys.modules[loader.name] = release
loader.exec_module(release)


class ForkPolicyTests(unittest.TestCase):
    def test_repo_identity_and_wrong_destinations(self):
        for value in ('BradHawthorne/gssquared', 'https://github.com/BradHawthorne/gssquared.git',
                      'git@github.com:BradHawthorne/gssquared.git',
                      'ssh://git@github.com/BradHawthorne/gssquared.git'):
            self.assertEqual(release.require_fork_repo(value), release.DEFAULT_GH_REPO)
        for value in ('jawaidbazyar2/gssquared', 'someone/gssquared',
                      'https://github.com.evil/BradHawthorne/gssquared', ''):
            with self.assertRaises(ValueError):
                release.require_fork_repo(value)

    def test_environment_cannot_redirect_upload(self):
        with patch.dict(os.environ, {'GS2_GH_REPO':'jawaidbazyar2/gssquared'}):
            with self.assertRaises(ValueError):
                release.detect_gh_repo(Path('.'))
        with patch.dict(os.environ, {'GS2_GH_REPO':'BradHawthorne/gssquared'}):
            self.assertEqual(release.detect_gh_repo(Path('.')), release.DEFAULT_GH_REPO)

    def test_missing_origin_has_no_fallback(self):
        with patch.dict(os.environ, {}, clear=True), patch.object(release.subprocess, 'check_output',
                side_effect=subprocess.CalledProcessError(2, ['git'])):
            with self.assertRaises(subprocess.CalledProcessError):
                release.detect_gh_repo(Path('.'))

    def test_origin_must_be_github_fork_not_owner_shaped_local_path(self):
        for url in ('BradHawthorne/gssquared', 'https://github.com/jawaidbazyar2/gssquared.git'):
            with patch.object(release.subprocess,'check_output',return_value=url):
                with self.assertRaises(ValueError):
                    release.fork_origin(Path('.'))

    def test_wrong_upload_refuses_before_artifacts_or_gh(self):
        with patch.object(release, 'dist_dir', side_effect=AssertionError('artifact inspection')), \
                contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(release.cmd_upload('fixture', True, 'jawaidbazyar2/gssquared'), 2)

    def test_wrong_environment_refuses_before_artifacts_or_gh(self):
        with patch.dict(os.environ, {'GS2_GH_REPO':'jawaidbazyar2/gssquared'}), \
                patch.object(release, 'dist_dir', side_effect=AssertionError('artifact inspection')), \
                contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(release.cmd_upload('fixture', True, None), 2)

    def test_upload_and_create_commands_bind_fork(self):
        with tempfile.TemporaryDirectory(prefix='gs2-fork-release-') as directory:
            dest=Path(directory)
            (dest/'fixture.zip').write_bytes(b'fixture')
            for exists in (True, False):
                calls=[]
                def fake_run(argv, **kwargs):
                    calls.append(argv)
                    return subprocess.CompletedProcess(argv, 0 if exists or 'view' not in argv else 1)
                with patch.object(release, 'dist_dir', return_value=dest), \
                        patch.object(release, 'verify_artifact', return_value=(True, 'fixture')), \
                        patch.object(release.shutil, 'which', return_value='fixture-gh'), \
                        patch.object(release.subprocess, 'run', side_effect=fake_run), \
                        contextlib.redirect_stdout(io.StringIO()):
                    self.assertEqual(release.cmd_upload('fixture', True, release.DEFAULT_GH_REPO),0)
                self.assertEqual(len(calls),2)
                for argv in calls:
                    self.assertEqual(argv[argv.index('--repo')+1], release.DEFAULT_GH_REPO)
                self.assertIn('upload' if exists else 'create', calls[-1])

    def test_host_guards_refuse_wrong_or_missing_origin_before_effects(self):
        with tempfile.TemporaryDirectory(prefix='gs2-fork-origin-') as directory:
            repo=Path(directory)
            subprocess.run(['git','init','-q',str(repo)],check=True)
            subprocess.run(['git','-C',str(repo),'remote','add','origin',
                            'https://github.com/BradHawthorne/gssquared.git'],check=True)
            host=release.Host('macos','local',str(repo),str(repo))
            good=release.capture_on_host(host, 'printf accepted > accepted.txt')
            self.assertEqual(good.returncode,0,good.stderr)
            self.assertTrue((repo/'accepted.txt').exists())
            for url in ('https://github.com/jawaidbazyar2/gssquared.git',
                        'https://github.com.evil/BradHawthorne/gssquared.git'):
                subprocess.run(['git','-C',str(repo),'remote','set-url','origin',url],check=True)
                refused=release.capture_on_host(host,'printf wrong > refused.txt')
                self.assertEqual(refused.returncode,2,refused.stderr)
                self.assertFalse((repo/'refused.txt').exists())
                with contextlib.redirect_stdout(io.StringIO()):
                    self.assertEqual(release.run_on_host(host,'printf wrong > refused.txt',None),2)
                self.assertFalse((repo/'refused.txt').exists())
            subprocess.run(['git','-C',str(repo),'remote','remove','origin'],check=True)
            self.assertNotEqual(release.capture_on_host(host,'printf wrong > refused.txt').returncode,0)
            self.assertFalse((repo/'refused.txt').exists())

    def test_remote_wrapper_uses_same_guard_and_ucrt(self):
        host=release.Host('windows','ssh','/fixture/source','/fixture/release',
                          host='fixture',remote_shell='msys2')
        wrapper=release.wrap_remote_script(host,'echo fixture')
        self.assertIn(release.ORIGIN_GUARD,wrapper)
        self.assertLess(wrapper.index(release.ORIGIN_GUARD),wrapper.index('echo fixture'))
        self.assertIn('export PATH=/ucrt64/bin:$PATH',wrapper)
        self.assertIn('-G Ninja',release.BUILD_SCRIPT['windows'])
        self.assertNotIn('origin/main',release.UNPUSHED_SCRIPT)

    def test_sync_defaults_to_fork_ref_without_network(self):
        host=release.Host('macos','local',str(Path(__file__).parent),'/fixture/release')
        with patch.object(release,'fork_origin') as origin, \
                patch.object(release.subprocess,'run') as run, \
                patch.object(release,'git_sha',return_value='fixture-sha') as sha, \
                patch.object(release,'ensure_on_origin'), \
                patch.object(release,'load_status',return_value={}), \
                patch.object(release,'save_status'), \
                contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(release.cmd_sync({'macos':host},(),None,False),0)
        origin.assert_called_once()
        self.assertEqual(sha.call_args.args[1],'origin/a2gspu')
        self.assertEqual(run.call_args.args[0],['git','fetch','origin','--tags'])

    def test_wrong_sync_origin_refuses_before_fetch(self):
        host=release.Host('macos','local',str(Path(__file__).parent),'/fixture/release')
        with patch.object(release,'fork_origin',side_effect=ValueError('non-fork')), \
                patch.object(release.subprocess,'run',side_effect=AssertionError('network')):
            with self.assertRaises(ValueError):
                release.cmd_sync({'macos':host},(),None,False)


if __name__ == '__main__':
    unittest.main(verbosity=2)
