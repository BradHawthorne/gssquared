"""Live IIe/IIgs tests for snapshot diffs; invoked by the agentic gate."""
import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import sys

spec=importlib.util.spec_from_file_location("p0check",Path(__file__).with_name("p0check.py"))
harness=importlib.util.module_from_spec(spec)
spec.loader.exec_module(harness)


def run(exe,platform):
    s=harness.Session(exe,platform)
    try:
        # Quotes and Windows backslashes must survive the three-path parser.
        a,b,out=(s.dir/name for name in ("before state.snap","after state.snap","state diff.json"))
        s.cmd("poke 000812 00")
        s.cmd("setreg a 0000")
        s.cmd(f"save {a}")
        s.cmd(f'diff "{a}" "{a}" "{out}"')
        data=json.loads(out.read_text())
        harness.check(data["equal"] and not data["registers"] and not data["sections"],f"platform {platform}: identical snapshot has empty state diff")
        s.cmd("poke 000812 7B")
        s.cmd("setreg a 0042")
        s.cmd(f"save {b}")
        before=s.cmd("cpu")
        result=s.cmd(f'diff "{a}" "{b}" "{out}"')
        after=s.cmd("cpu")
        data=json.loads(out.read_text())
        harness.check(before==after,"snapshot diff leaves live CPU unchanged")
        harness.check("equal=0" in result and data["registers"]==[{"name":"A","before":0,"after":66}],"changed A reported by name/value")
        expected="iigs.ram" if platform==5 else "iie.ram"
        memory=next(row for row in data["sections"] if row["name"]==expected)
        harness.check(memory["changed_bytes"]==1 and memory["ranges"]==[
            {"begin":0x812,"end":0x813,"before_hex":"00","after_hex":"7B"}],"planted RAM byte located exactly in physical domain")
        harness.check(not data["equal"] and data["schema"]=="a2snapdiff-v1","diff artifact names its schema/model")
        cli=exe.with_name("snapdiff.exe" if sys.platform=="win32" else "snapdiff")
        for left,right,flag,rc in [(a,a,[],0),(a,b,[],1),(a,a,["--expect-different"],1),(a,b,["--expect-different"],0)]:
            result=subprocess.run([str(cli),str(left),str(right),*flag],capture_output=True,timeout=20)
            harness.check(result.returncode==rc,"CLI equality/difference exit contract and negative expectation")
        original=a.read_bytes()
        harness.check("status=FAIL" in s.cmd(f'diff "{a}" "{b}" "{a}"',ok=False),"input overwrite refused")
        harness.check(a.read_bytes()==original,"refused report did not modify input")
        malformed=s.dir/"truncated.snap"
        malformed.write_bytes(original[:-1])
        untouched=s.dir/"untouched.json"
        untouched.write_text("sentinel")
        harness.check("status=FAIL" in s.cmd(f'diff "{malformed}" "{b}" "{untouched}"',ok=False),"truncated manifest refused before report output")
        harness.check(untouched.read_text()=="sentinel","invalid input preserves prior report")
        missing=s.dir/"missing.snap"
        harness.check("status=FAIL" in s.cmd(f'diff "{missing}" "{b}" "{out}"',ok=False),"missing snapshot fails loud")
        # A file report failure must not masquerade as a successful comparison.
        harness.check("status=FAIL" in s.cmd(f'diff "{a}" "{b}" "{s.dir / "absent" / "out.json"}"',ok=False),"unwritable report path fails loud")
    finally:
        s.close()


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe",type=Path,required=True)
    exe=parser.parse_args().exe.resolve()
    for platform in (3,5):
        run(exe,platform)
    print("snapshot checks: ALL GREEN")
