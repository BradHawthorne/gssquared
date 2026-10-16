"""Foundation-local P0 checks, with independent expected values and refusals.

python -I tools/a2rail/p0check.py --exe <GSSquared.exe>
Each process owns its scratch directory and is stopped with CTRL/protocol quit.
"""
import argparse
import copy
import importlib.util
import json
import os
from pathlib import Path
import re
import socket
import subprocess
import struct
import sys
import tempfile
import time
import threading

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "clients/python/src"))
from gs2debug import Client, ProtocolError
from gs2debug.types import MANIFEST

spec = importlib.util.spec_from_file_location("a2profile", Path(__file__).with_name("profile.py"))
profile = importlib.util.module_from_spec(spec)
spec.loader.exec_module(profile)


def check(value, label):
    if not value:
        raise AssertionError(label)
    print("PASS", label, flush=True)


def environment():
    env = {k: v for k, v in os.environ.items() if not k.startswith(("A2GSPU_", "MEMVU_", "WIZ5_"))}
    env.update(A2GSPU_SEED="1", A2GSPU_FAKETIME="1000000000", A2GSPU_BRAM="default",
               A2GSPU_BACKGROUND_WINDOW="1")
    return env


class Session:
    def __init__(self, exe, platform, rails=None,configfile=None):
        self.temp = tempfile.TemporaryDirectory(prefix="a2p0-")
        self.dir = Path(self.temp.name)
        env = environment()
        env.update(A2GSPU_CTRL=str(self.dir), A2GSPU_CTRL_ATOMIC="1", A2GSPU_CTRL_TIMEOUT="30")
        env.update(rails or {})
        self.log = (self.dir / "session.log").open("wb")
        selection=[str(configfile)] if configfile is not None else ['-p',str(platform)]
        self.process = subprocess.Popen([str(exe),*selection, "-n", "--no-quit-confirm"],
                                        cwd=self.dir, env=env, stdout=self.log, stderr=subprocess.STDOUT)
        self.seq = 0

    def cmd(self, command, ok=True):
        self.seq += 1
        pending = self.dir / "pending"
        pending.write_text(command+"\n", encoding="utf-8")
        pending.replace(self.dir / f"cmd.{self.seq}")
        ack = self.dir / f"ack.{self.seq}"
        deadline = time.monotonic()+20
        while time.monotonic() < deadline:
            try:
                result = ack.read_text(encoding="utf-8")
                if result:
                    if ok and "status=OK" not in result:
                        raise AssertionError(f"{command}: {result}")
                    return result
            except (FileNotFoundError, PermissionError):
                pass
            if self.process.poll() is not None:
                raise AssertionError(f"session exited {self.process.returncode}: {(self.dir/'session.log').read_text(errors='replace')}")
            time.sleep(.02)
        raise TimeoutError(command)

    def code(self, data, address=0x800):
        path = self.dir / "code.bin"
        path.write_bytes(bytes(data))
        self.cmd(f"load {address:06X} {path}")
        self.cmd(f"setreg pc {address:06X}")

    def close(self):
        try:
            if self.process.poll() is None:
                self.cmd("quit")
                self.process.wait(timeout=10)
            check(self.process.returncode == 0, "session exits cleanly")
        finally:
            if self.process.poll() is None:
                self.process.kill() # only our owned process, --no-quit-confirm
                self.process.wait(timeout=5)
            self.log.close()
            self.temp.cleanup()


def counts(exe, platform):
    s = Session(exe, platform)
    try:
        check("status=FAIL" in s.cmd("cov counts on", ok=False), "counts refuses unarmed coverage")
        s.cmd("cov on 0800-08FF")
        s.cmd("cov counts on")
        # Seven instructions, 16 cycles: LDX #3; DEX; BNE DEX (3 iterations).
        s.code([0xA2, 3, 0xCA, 0xD0, 0xFD, 0xEA])
        s.cmd("setreg e 1") if platform == 5 else None
        s.cmd("setreg p 34")
        result = s.cmd("run-until 000805 100")
        check("instructions=7" in result and "cycles=16 " in result, f"platform {platform} independent loop instruction/cycle totals: {result.strip()}")
        out = s.dir / "counts.json"
        s.cmd(f"cov counts write {out}")
        data = json.loads(out.read_text())
        symbols = {"schema": "a2symbols-v1", "functions": [{"name": "loop", "start": 0x802, "end": 0x805}]}
        rolled = profile.rollup(data, symbols)
        check(rolled["instructions"] == 7 and rolled["cycles"] == 16 and
              rolled["functions"][0] == {"name": "loop", "instructions": 6, "cycles": 14}, "per-function roll-up includes repeated PCs")
        wrong = subprocess.run([sys.executable, "-I", str(Path(__file__).with_name("profile.py")), str(out),
                                "--expect-instructions", "8"], capture_output=True)
        check(wrong.returncode == 1, "planted off-by-one expectation fails")
        result = s.cmd("step 1")
        check("instructions=1" in result and "cycles=2 " in result, "step reports actual instructions")
        s.cmd("cov reset")
        s.cmd(f"cov counts write {out}")
        check(json.loads(out.read_text())["pcs"] == [], "reset clears per-PC counts")
        # Independently construct an A2MAP1 binary plan; it supplies segment
        # identity while function names/ranges come from a separate symbol file.
        plan = s.dir/"loop.map"
        program = bytes([0xA2,3,0xCA,0xD0,0xFD,0xEA])
        header = bytearray(56)
        header[:8] = b"A2MAP1\0\0"
        struct.pack_into("<IIQ",header,8,1,1,len(program))
        plan.write_bytes(header+struct.pack("<III",0,0x800,len(program))+program)
        s.cmd(f"cov map {plan}")
        s.cmd("cov counts on")
        s.code(program)
        s.cmd("run-until 000805 100")
        s.cmd(f"cov counts write {out}")
        mapped = json.loads(out.read_text())
        symbols["map_hash"] = mapped["map_hash"]
        check(profile.rollup(mapped,symbols,plan)["instructions"] == 7,"mapped profile identity and symbol roll-up")
        damaged = bytearray(plan.read_bytes())
        damaged[-1] ^= 1
        plan.write_bytes(damaged)
        try:
            profile.rollup(mapped,symbols,plan)
        except ValueError:
            check(True,"stale binary plan rejected by profile reader")
        else:
            raise AssertionError("stale binary plan accepted")
        s.cmd("poke 000802 EA")
        s.cmd("setreg pc 000802")
        s.cmd("step 1")
        check("status=FAIL" in s.cmd(f"cov counts write {out}",ok=False),"executed-code mismatch refuses mapped counts")
        s.cmd("cov off")
        check("status=FAIL" in s.cmd(f"cov counts write {out}", ok=False), "unarmed counts write refuses success")
        result = s.cmd("run 0",ok=False)
        check("status=FAIL" in result and "instructions=0" in result and "cycles=0" in result,
              "zero-frame run refuses success and reports zero work")
    finally:
        s.close()


def console(exe, armed):
    with tempfile.TemporaryDirectory(prefix="a2wdm-") as tmp:
        out = Path(tmp) / "wdm.out"
        s = Session(exe, 5, {"A2GSPU_WDM_CONSOLE": "file:"+str(out)} if armed else {})
        try:
            # Enter native M16/X16, point to "abc", string then byte output.
            program = [0x18,0xFB,0xC2,0x30,0xA9,0x00,0x09,0xA2,0,0,0xA0,3,0,
                       0x42,0xFD,0xE2,0x20,0xA9,ord('!'),0x42,0xFE,0xEA]
            s.code(program)
            s.cmd("poke 000900 61 62 63")
            result = s.cmd("run-until 000815 100")
            check("instructions=10" in result, "WDM runs via existing operand handlers")
            check((out.read_bytes() == b"abc!") if armed else not out.exists(), "WDM output matches when armed; absent when disabled")
            # Both WDMs are still two-cycle operations, preserving state.
            s.cmd("setreg pc 000813")
            result = s.cmd("step 1")
            check("cycles=2 " in result and "A=21" in result, "WDM is two cycles and preserves A")
            if armed:
                reader = s.cmd("rail read A2GSPU_WDM_CONSOLE")
                check("bytes=5" in reader, "console readback counts output")
        finally:
            s.close()


def serial(exe):
    with tempfile.TemporaryDirectory(prefix="a2scc-") as tmp:
        out_a, out_b = Path(tmp)/"a.out", Path(tmp)/"b.out"
        s = Session(exe, 5, {"A2GSPU_SCC_A":"file:"+str(out_a), "A2GSPU_SCC_B":"file:"+str(out_b)})
        try:
            code = [0x78]  # SEI, then SCC A at C039/C03B
            def store(address, byte):
                code.extend([0xA9,byte,0x8D,address&255,address>>8])
            for register, value in [(9,0xC0),(4,0x44),(11,0x50),(12,10),(13,0),(14,1),(5,0x68)]:
                store(0xC039, register)
                store(0xC039, value)
            for byte in b"SCC\n":
                store(0xC03B,byte)
                code.extend([0xAD,0x39,0xC0,0x29,4,0xF0,0xF9])
            end = 0x800+len(code)
            code.extend([0x4C,end&255,end>>8])
            s.code(code)
            s.cmd(f"run-until {end:06X} 20000")
            run = s.cmd("run 2") # let the final shift-register byte complete
            instructions = re.search(r"instructions=(\d+)",run)
            check(instructions and int(instructions[1]) > 0,"frame run reports actual opcode executions")
            check(out_a.read_bytes() == b"SCC\n", "completed SCC A TX bytes captured exactly")
            check(out_b.read_bytes() == b"", "wrong SCC channel stays empty (negative capture oracle)")
            check("bytes=4" in s.cmd("rail read A2GSPU_SCC_A"), "SCC readback names emitted bytes")
            s.code([0xA9,14,0x8D,0x39,0xC0,0xA9,0x11,0x8D,0x39,0xC0,
                    0xA9,ord('x'),0x8D,0x3B,0xC0,0x4C,0x0F,0x08])
            s.cmd("run 2")
            check(out_a.read_bytes() == b"SCC\n","local SCC loopback does not appear on TX wire capture")
        finally:
            s.close()


def validate_manifest(value):
    expected = {1,2,3,4,5,6,7,8,9,0x101,0x102,0x103,0x104,0x105,0x201,0x202,0x203,
                0x301,0x302,0x303,0x401,0x402,0x403,0x404,0x405,0x501,0x502,0x601,0x602,0x701,0x801,0x802}
    ids = [row["id"] for row in value["types"]]
    if value["schema"] != "gs2-debug-manifest-v1" or set(ids) != expected or len(ids) != len(expected):
        raise ValueError("socket manifest type set differs")


def socket_check(exe):
    with tempfile.TemporaryDirectory(prefix="a2sock-") as tmp:
        path = str(Path(tmp)/"debug.sock")
        with (Path(tmp)/"session.log").open("wb") as log:
            p = subprocess.Popen([str(exe),"--debug",path,"-p","5","-n","--no-quit-confirm"],
                                 cwd=tmp,env=environment(),stdout=log,stderr=subprocess.STDOUT)
            c = Client()
            try:
                deadline = time.monotonic()+20
                while True:
                    try:
                        c.connect(path)
                        break
                    except OSError:
                        if time.monotonic()>deadline or p.poll() is not None:
                            raise AssertionError("debug socket did not start")
                        time.sleep(.05)
                c.hello()
                value = c.manifest()
                validate_manifest(value)
                check(value["frame_bytes"] == 12 and value["protocol_version"] == 1, "socket manifest covers original types and native agent extensions")
                bad = copy.deepcopy(value)
                bad["types"].pop()
                try:
                    validate_manifest(bad)
                except ValueError:
                    check(True,"planted missing socket type fails discovery oracle")
                else:
                    raise AssertionError("missing type accepted")
                try:
                    c.request(MANIFEST,b"bad")
                except ProtocolError:
                    check(True,"MANIFEST refuses nonempty payload")
                else:
                    raise AssertionError("MANIFEST bad length accepted")
                c.ping()
                c.quit()
                p.wait(timeout=10)
                check(p.returncode == 0,"socket quit exits cleanly")
            finally:
                c.close()
                if p.poll() is None:
                    p.kill()
                    p.wait(timeout=5)


def binding_failures(exe):
    for platform, rails in [
        (3, {"A2GSPU_SCC_A":"file:a.out"}),
        (3, {"A2GSPU_WDM_CONSOLE":"file:w.out"}),
        (5, {"A2GSPU_SCC_A":"bad-binding"}),
        (5, {"A2GSPU_WDM_CONSOLE":"file:missing-directory/out"}),
        (5, {"A2GSPU_SCC_A":"pipe:missing-endpoint"}),
        (5, {"A2GSPU_SCC_A":"file:a.out", "A2GSPU_SCC_B":"file:./a.out"}),
    ]:
        with tempfile.TemporaryDirectory(prefix="a2bad-") as tmp:
            env = environment()
            env.update(rails)
            env.update(A2GSPU_SPIKE_FRAMES="1")
            result = subprocess.run([str(exe),"-p",str(platform),"-n","--no-quit-confirm"],
                                    cwd=tmp,env=env,capture_output=True,timeout=20)
            if result.returncode != 2 or b"FAIL" not in result.stderr:
                raise AssertionError(f"binding refusal rc={result.returncode}: {rails}: {result.stderr[-500:]!r}")
            check(True,f"invalid/unsupported binding fails loud: {platform} {rails}")


def pipe_check(exe):
    if os.name != "nt":
        # Windows named pipes are exercised on the development host. On POSIX,
        # hold a FIFO reader before the emulator opens its nonblocking writer.
        with tempfile.TemporaryDirectory(prefix="a2fifo-") as tmp:
            path = Path(tmp)/"console"
            os.mkfifo(path)
            fd = os.open(path,os.O_RDONLY|os.O_NONBLOCK)
            try:
                s = Session(exe,5,{"A2GSPU_WDM_CONSOLE":"pipe:"+str(path)})
                try:
                    s.code([0xA9,ord('P'),0x42,0xFE,0xEA])
                    s.cmd("step 2")
                    check(os.read(fd,16) == b"P","console FIFO output")
                finally:
                    s.close()
            finally:
                os.close(fd)
        return
    import ctypes
    from ctypes import wintypes
    api = ctypes.WinDLL("kernel32",use_last_error=True)
    api.CreateNamedPipeW.argtypes = [wintypes.LPCWSTR,wintypes.DWORD,wintypes.DWORD,wintypes.DWORD,
                                     wintypes.DWORD,wintypes.DWORD,wintypes.DWORD,wintypes.LPVOID]
    api.CreateNamedPipeW.restype = wintypes.HANDLE
    api.ConnectNamedPipe.argtypes = [wintypes.HANDLE,wintypes.LPVOID]
    api.ReadFile.argtypes = [wintypes.HANDLE,wintypes.LPVOID,wintypes.DWORD,
                             ctypes.POINTER(wintypes.DWORD),wintypes.LPVOID]
    api.CloseHandle.argtypes = [wintypes.HANDLE]
    name = "\\\\.\\pipe\\a2p0-"+str(os.getpid())+"-"+str(time.monotonic_ns())
    handle = api.CreateNamedPipeW(name,1,0,1,4096,4096,0,None)
    check(handle != wintypes.HANDLE(-1).value,"named-pipe fixture created")
    received = []
    def read_pipe():
        if not api.ConnectNamedPipe(handle,None) and ctypes.get_last_error() != 535:
            return
        data = ctypes.create_string_buffer(16)
        size = wintypes.DWORD()
        if api.ReadFile(handle,data,16,ctypes.byref(size),None):
            received.append(data.raw[:size.value])
    thread = threading.Thread(target=read_pipe,daemon=True)
    thread.start()
    try:
        s = Session(exe,5,{"A2GSPU_WDM_CONSOLE":"pipe:"+name})
        try:
            s.code([0xA9,ord('P'),0x42,0xFE,0xEA])
            s.cmd("step 2")
            thread.join(5)
            check(received == [b"P"],"console named-pipe output reaches reader")
        finally:
            s.close()
    finally:
        api.CloseHandle(handle)
        thread.join(1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe",type=Path,required=True)
    args = parser.parse_args()
    profile.selftest()
    for tool in ('ctrlclientcheck.py','guestviewcheck.py','outputassertcheck.py'):
        subprocess.run([sys.executable,'-I',str(Path(__file__).with_name(tool))],check=True)
    for platform in (3,5):
        counts(args.exe.resolve(),platform)
    console(args.exe.resolve(),True)
    console(args.exe.resolve(),False)
    serial(args.exe.resolve())
    binding_failures(args.exe.resolve())
    pipe_check(args.exe.resolve())
    socket_check(args.exe.resolve())
    subprocess.run([sys.executable,"-I",str(Path(__file__).with_name("snapshotcheck.py")),
                    "--exe",str(args.exe.resolve())],check=True)
    subprocess.run([sys.executable,"-I",str(Path(__file__).with_name("interfacecheck.py")),
                    "--exe",str(args.exe.resolve())],check=True)
    subprocess.run([sys.executable,"-I",str(Path(__file__).with_name("guestlivecheck.py")),
                    "--exe",str(args.exe.resolve())],check=True)
    for tool in ('capturecheck.py','contentcheck.py','faultcheck.py','inputcheck.py','devicecoveragecheck.py','socketstreamcheck.py','lifecyclecheck.py','sockettoolscheck.py','uicheck.py','mcpcheck.py','contextcheck.py','experimentcheck.py'):
        subprocess.run([sys.executable,'-I',str(Path(__file__).with_name(tool)), '--exe',str(args.exe.resolve())],check=True)
    print("P0 checks: ALL GREEN")


if __name__ == "__main__":
    main()
