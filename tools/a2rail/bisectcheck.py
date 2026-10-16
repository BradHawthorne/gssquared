"""P2.4 evidence: real tiny CMake builds over synthetic dangling Git commits."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

spec=importlib.util.spec_from_file_location("revision_bisect",Path(__file__).with_name("revisionbisect.py"))
subject=importlib.util.module_from_spec(spec)
spec.loader.exec_module(subject)

CMAKE='''cmake_minimum_required(VERSION 3.10)
project(bisect_fixture LANGUAGES CXX)
file(READ "${CMAKE_SOURCE_DIR}/value.txt" VALUE)
configure_file(probe.cpp.in probe.cpp @ONLY)
add_executable(probe "${CMAKE_BINARY_DIR}/probe.cpp")
enable_testing()
add_test(NAME fixture_smoke COMMAND probe 0)
'''
PROBE=r'''#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>
int main(int argc,char **argv) {
  int v=@VALUE@;
  if(v==98)std::this_thread::sleep_for(std::chrono::seconds(3));
  if(v==99)v=argc>1?std::atoi(argv[1]):0;
  std::cout<<"A2GSPU PINS: seed=1 faketime=1780000000 ramdisk=1 bram=default/1234567890ABCDEF pace=fast platform=3 frames=2 rom=1/1234567890ABCDEF build=fixture exe=fixture machine=fixture/p3 slots=-\n";
  std::cout<<"GSDIAG: status=OK hash="<<std::hex<<std::uppercase<<std::setfill('0')<<std::setw(16)<<(v+1)
    <<" cycles="<<std::dec<<10<<" run="<<std::hex<<std::setw(16)<<(v+1)<<"\n";
}
'''

class BisectTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix="a2bisect-check-")
        cls.root=Path(cls.temp.name)
        cls.repo=cls.root/"objects.git"
        subprocess.run(["git","init","--bare",str(cls.repo)],check=True,capture_output=True)
        cls.cmake=shutil.which("cmake")
        cls.cxx=shutil.which("g++") if os.name=="nt" else shutil.which("c++")
        cls.ninja=shutil.which("ninja")
        if not all((cls.cmake,cls.cxx,cls.ninja)):
            raise RuntimeError("bisect selftest needs cmake, C++ compiler and ninja on PATH")
        cls.recipe={"schema":"a2bisect-scenario-v1","frames":2,"repeats":2,
            "configure":[cls.cmake,"-S","{source}","-B","{build}","-G","Ninja",
                         "-DCMAKE_CXX_COMPILER="+cls.cxx,"-DCMAKE_MAKE_PROGRAM="+cls.ninja],
            "build":[cls.cmake,"--build","{build}","--parallel","2"],
            "checks":[[shutil.which("ctest"),"--test-dir","{build}","--output-on-failure"]],
            "executable":"probe.exe" if os.name=="nt" else "probe",
            "arguments":["{repeat}"],"run_timeout_seconds":10,"build_timeout_seconds":60}
        cls.counter=0

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def git(self,*args,data=None):
        env=dict(os.environ,GIT_AUTHOR_NAME="Fixture",GIT_AUTHOR_EMAIL="fixture@example.invalid",
                 GIT_COMMITTER_NAME="Fixture",GIT_COMMITTER_EMAIL="fixture@example.invalid")
        return subprocess.run(["git","--git-dir",str(self.repo),*args],input=data,
                              capture_output=True,check=True,env=env).stdout.strip().decode()

    def chain(self,values):
        commits=[]
        for i,value in enumerate(values):
            files={"CMakeLists.txt":CMAKE,"probe.cpp.in":PROBE,"value.txt":str(value)}
            entries=[]
            for name,text in sorted(files.items()):
                blob=self.git("hash-object","-w","--stdin",data=text.encode())
                entries.append(f"100644 blob {blob}\t{name}\n")
            tree=self.git("mktree",data="".join(entries).encode())
            args=["-c","commit.gpgSign=false","commit-tree",tree]
            if commits:args += ["-p",commits[-1]]
            commit=self.git(*args,data=f"fixture {i}\n".encode())
            commits.append(commit)
        return commits

    def run_case(self,values,recipe=None):
        commits=self.chain(values)
        type(self).counter+=1
        scenario=self.root/f"scenario-{self.counter}.json"
        scenario.write_text(json.dumps(recipe or self.recipe))
        out=self.root/f"run-{self.counter}"
        result=subject.execute(self.repo,commits[0],commits[-1],scenario,out)
        return result,commits,out

    def test_first_change_and_provenance(self):
        fixture=self.root/"input.bin";fixture.write_bytes(b"fixed input\x00")
        recipe=dict(self.recipe,inputs=[{"source":str(fixture),"destination":"disk.bin"},
            {"source":str(fixture),"destination":"fixtures/input.bin","scope":"source"}])
        result,commits,out=self.run_case([0,0,1,1],recipe)
        self.assertEqual(result["result"],"change")
        self.assertEqual(result["first_changed"],commits[2])
        self.assertTrue(result["earlier_revisions_verified"])
        self.assertTrue(result["source_unchanged"])
        self.assertEqual(result["observations"][commits[0]]["checks"][0]["returncode"],0)
        source=out/"revisions"/commits[2]/"source"
        self.assertEqual((source/"fixtures/input.bin").read_bytes(),fixture.read_bytes())
        self.assertEqual((source.parent/"run-0/disk.bin").read_bytes(),fixture.read_bytes())
        self.assertTrue(result["dependency_inputs_unchanged"])
        self.assertEqual(result["inputs"][0]["sha256"],subject.sha(fixture))
        self.assertEqual(subprocess.check_output(["git","-C",str(source),"rev-parse","HEAD"]).decode().strip(),commits[2])
        # The planted wrong answer must not pass the independent expectation.
        self.assertNotEqual(result["first_changed"],commits[1])

    def test_no_change(self):
        result,commits,_=self.run_case([0,0,0,0])
        self.assertEqual(result["result"],"none")
        self.assertEqual(set(result["observations"]),set(commits))

    def test_cli_wrong_expectation_fails(self):
        commits=self.chain([0,1])
        type(self).counter+=1
        scenario=self.root/f"scenario-{self.counter}.json"
        scenario.write_text(json.dumps(self.recipe))
        out=self.root/f"run-{self.counter}"
        result=subprocess.run([sys.executable,"-I",str(Path(subject.__file__)),"--repo",str(self.repo),
            "--before",commits[0],"--after",commits[-1],"--scenario",str(scenario),
            "--scratch",str(out),"--expect","none"],capture_output=True)
        self.assertEqual(result.returncode,1,result.stderr.decode(errors="replace"))
        report=json.loads((out/"report.json").read_text())
        self.assertEqual(report["first_changed"],commits[1])
        self.assertTrue(report["source_unchanged"])

    def test_endpoint_reversion_is_not_none(self):
        result,commits,_=self.run_case([0,1,0,0])
        self.assertEqual(result["first_changed"],commits[1])
        self.assertTrue(result["endpoint_equal"])

    def test_build_failure_is_inconclusive(self):
        commits=self.chain([0,"deliberately_invalid_cpp",1,1])
        type(self).counter+=1
        scenario=self.root/f"scenario-{self.counter}.json"
        scenario.write_text(json.dumps(self.recipe))
        out=self.root/f"run-{self.counter}"
        with self.assertRaises(subject.BisectError):subject.execute(self.repo,commits[0],commits[-1],scenario,out)
        report=json.loads((out/"report.json").read_text())
        self.assertEqual(report["result"],"inconclusive")
        self.assertIn("build failed",report["error"])
        self.assertNotIn("first_changed",report)

    def test_nondeterminism_and_timeout(self):
        for value in (99,98):
            recipe=dict(self.recipe,checks=[],run_timeout_seconds=.15 if value==98 else 10)
            with self.assertRaisesRegex(subject.BisectError,"timed out" if value==98 else "nondeterministic"):
                self.run_case([0,value],recipe)

    def test_scratch_and_recipe_refusals(self):
        commits=self.chain([0,1])
        path=self.root/"invalid.json"
        path.write_text(json.dumps(self.recipe))
        existing=self.root/"existing";existing.mkdir(exist_ok=True)
        with self.assertRaises(subject.BisectError):subject.execute(self.repo,commits[0],commits[-1],path,existing)
        self.assertFalse((existing/"report.json").exists())
        bad=dict(self.recipe,executable="../outside")
        path.write_text(json.dumps(bad))
        with self.assertRaises(subject.BisectError):subject.execute(self.repo,commits[0],commits[-1],path,self.root/"invalid-run")

    def test_strict_output_parser(self):
        sample="A2GSPU PINS: seed=1 faketime=1780000000 ramdisk=1 bram=default/1234567890ABCDEF pace=fast platform=3 frames=2 rom=1/1234567890ABCDEF machine=fixture/p3 slots=- build=fixture exe=fixture\nGSDIAG: status=OK hash=0000000000000001 cycles=10 run=0000000000000001\n"
        self.assertEqual(subject.parse_output(sample)[0]["cycles"],10)
        for bad in (sample+sample,sample.replace("run=0000000000000001","run=wrong"),"",sample.replace("cycles=10","cycles=-1"),sample.replace("seed=1","seed=unset"),sample.replace(" rom=1/1234567890ABCDEF","")):
            with self.assertRaises(subject.BisectError):subject.parse_output(bad)

    def test_dependency_snapshot(self):
        source=self.root/"dependency";source.mkdir()
        (source/".git").write_text("metadata excluded")
        (source/"data").write_bytes(b"initial")
        snapshot=self.root/"dependency-copy"
        before=subject.tree_snapshot(source,snapshot)
        self.assertEqual(before,subject.tree_snapshot(snapshot))
        self.assertFalse((snapshot/".git").exists())
        (source/"data").write_bytes(b"local changes")
        self.assertNotEqual(before,subject.tree_snapshot(source))

if __name__=="__main__":
    unittest.main(verbosity=2)
