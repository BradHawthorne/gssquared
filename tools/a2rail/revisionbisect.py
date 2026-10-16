"""Build local revisions in scratch and locate the first changed GSDIAG witness.

No checkout, stash, worktree, source ref/config write, fetch or push. Commands
are argv arrays, not shell text. See bisect.example.json and the a2rail README.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys
import tarfile
import time


class BisectError(RuntimeError):
    pass


def sha(path):
    h=hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda:stream.read(1024*1024),b""):h.update(block)
    return h.hexdigest()


def git(repo,*args,data=None):
    env=dict(os.environ,GIT_OPTIONAL_LOCKS="0")
    result=subprocess.run(["git","-C",str(repo),*args],input=data,capture_output=True,env=env)
    if result.returncode:
        raise BisectError(f"git {args!r}: {result.stderr.decode(errors='replace').strip()}")
    return result.stdout


def text_git(repo,*args):
    return git(repo,*args).decode().strip()


def inside(path,root):
    try:Path(path).resolve().relative_to(Path(root).resolve());return True
    except ValueError:return False


def relative(value):
    if not isinstance(value,str) or not value or "\\" in value or ":" in value:
        raise BisectError("expected a relative path using forward slashes")
    p=PurePosixPath(value)
    if p.is_absolute() or any(x in ("..",".git") for x in p.parts):
        raise BisectError("path escapes its scratch root or names .git")
    return Path(*p.parts)


def source_fingerprint(repo):
    """Refs/index/config and tracked/nonignored untracked bytes, excluding gitlinks.

    Symlink targets are recorded as text, never followed outside the source.
    Dependencies and scenario inputs have separate content fingerprints.
    """
    h=hashlib.sha256()
    common=Path(text_git(repo,"rev-parse","--git-common-dir"))
    if not common.is_absolute():common=repo/common
    gitdir=Path(text_git(repo,"rev-parse","--absolute-git-dir"))
    for path in (gitdir/"HEAD",gitdir/"index",common/"config"):
        h.update(str(path).encode());h.update(path.read_bytes() if path.is_file() else b"absent")
    h.update(git(repo,"for-each-ref","--format=%(refname) %(objectname)"))
    if text_git(repo,"rev-parse","--is-bare-repository")=="true":return h.hexdigest()
    files={}
    for row in git(repo,"ls-files","--stage","-z").split(b"\0"):
        if not row:continue
        meta,path=row.split(b"\t",1);mode=meta.split()[0]
        if mode==b"160000":h.update(row);continue
        files[path.decode("utf-8")]=mode
    for row in git(repo,"ls-files","--others","--exclude-standard","-z").split(b"\0"):
        if row:files[row.decode("utf-8")]=b"untracked"
    for name,mode in sorted(files.items()):
        path=repo/name;h.update(name.encode());h.update(b"\0"+mode+b"\0")
        if path.is_symlink():h.update(os.readlink(path).encode())
        elif path.is_file():h.update(bytes.fromhex(sha(path)))
        else:h.update(b"absent")
    return h.hexdigest()


def tree_snapshot(source,destination=None):
    """Copy/hash dependency bytes, without .git or escaping links/junctions."""
    source=Path(source).resolve();digest=hashlib.sha256();count=0;stack=set()
    def walk(path,rel):
        nonlocal count
        resolved=path.resolve()
        if not inside(resolved,source):raise BisectError(f"dependency link escapes snapshot root: {path}")
        if resolved in stack:raise BisectError(f"dependency directory cycle: {path}")
        if path.is_dir():
            stack.add(resolved);digest.update(b"dir\0"+rel.as_posix().encode()+b"\0")
            if destination is not None:(Path(destination)/rel).mkdir(parents=True,exist_ok=True)
            for item in sorted(path.iterdir()):
                if item.name!=".git":walk(item,rel/item.name)
            stack.remove(resolved)
        elif path.is_file():
            digest.update(rel.as_posix().encode()+b"\0"+bytes.fromhex(sha(path)));count+=1
            if destination is not None:shutil.copy2(path,Path(destination)/rel)
        else:raise BisectError(f"unsupported dependency entry: {path}")
    walk(source,Path())
    return {"sha256":digest.hexdigest(),"files":count}


def links(repo,revision):
    result={}
    for row in git(repo,"ls-tree","-rz",revision).split(b"\0"):
        if not row:continue
        meta,path=row.split(b"\t",1)
        mode,kind,oid=meta.split()
        if mode==b"160000":result[path.decode()]=oid.decode()
    return result


def command(value,label):
    if not isinstance(value,list) or not value or any(not isinstance(v,str) or not v or "\0" in v for v in value):
        raise BisectError(f"{label} must be a nonempty argv array of strings")
    return value


def recipe(path):
    try:cfg=json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError,ValueError) as exc:raise BisectError(f"cannot read scenario: {exc}") from exc
    if not isinstance(cfg,dict) or cfg.get("schema")!="a2bisect-scenario-v1":raise BisectError("unknown scenario schema")
    for label in ("configure","build"):command(cfg.get(label),label)
    if not isinstance(cfg.get("checks",[]),list):raise BisectError("checks must be a list")
    for check in cfg.get("checks",[]):command(check,"check")
    if not isinstance(cfg.get("arguments",[]),list) or any(not isinstance(v,str) for v in cfg.get("arguments",[])):
        raise BisectError("arguments must be a string array")
    relative(cfg.get("executable",""))
    for key,default,limit in (("frames",30,36000),("repeats",2,10),("max_revisions",64,4096)):
        cfg[key]=cfg.get(key,default)
        if type(cfg[key]) is not int or not (2 if key=="repeats" else 1)<=cfg[key]<=limit:raise BisectError(f"invalid {key}")
    for key,default in (("run_timeout_seconds",120),("build_timeout_seconds",900),("check_timeout_seconds",900)):
        cfg[key]=cfg.get(key,default)
        if type(cfg[key]) not in (int,float) or not 0<cfg[key]<=86400:raise BisectError(f"invalid {key}")
    cfg["accepted_exit_codes"]=cfg.get("accepted_exit_codes",[0])
    if (not isinstance(cfg["accepted_exit_codes"],list) or not cfg["accepted_exit_codes"] or
        any(type(v) is not int for v in cfg["accepted_exit_codes"])):raise BisectError("invalid accepted_exit_codes")
    env=cfg.get("env",{})
    reserved={"A2GSPU_CTRL","A2GSPU_COPILOT","A2GSPU_OUT_DIR","A2GSPU_SPIKE_FRAMES",
              "A2GSPU_SEED","A2GSPU_FAKETIME","A2GSPU_BRAM","A2GSPU_RAMDISK","A2GSPU_PACE"}
    if not isinstance(env,dict) or any(not isinstance(k,str) or not isinstance(v,str) or k in reserved or "\0" in k+v for k,v in env.items()):
        raise BisectError("invalid env or override of a reserved isolation/determinism pin")
    inputs=cfg.get("inputs",[])
    if not isinstance(inputs,list):raise BisectError("inputs must be a list")
    seen=set()
    for item in inputs:
        if not isinstance(item,dict) or not isinstance(item.get("source"),str):raise BisectError("invalid input source")
        rel=relative(item.get("destination",""));scope=item.get("scope","run")
        if scope not in ("run","source") or (scope,str(rel)) in seen:raise BisectError("invalid/duplicate input destination")
        seen.add((scope,str(rel)))
    return cfg


def parse_output(output):
    diag=re.findall(r"^GSDIAG: (.*)$",output,re.MULTILINE)
    pins=re.findall(r"^A2GSPU PINS: (.*)$",output,re.MULTILINE)
    if len(diag)!=1 or len(pins)!=1:raise BisectError("expected exactly one GSDIAG and one PINS line")
    def fields(line):
        pairs=re.findall(r"(?:^|\s)([a-zA-Z_]+)=([^\s]+)",line)
        if len(dict(pairs))!=len(pairs):raise BisectError("duplicate output field")
        return dict(pairs)
    row=fields(diag[0]);pin=fields(pins[0])
    required={"seed","faketime","ramdisk","bram","pace","platform","frames","rom","machine","slots","build","exe"}
    if not required<=pin.keys():raise BisectError("incomplete PINS identity")
    if (pin["seed"]!="1" or pin["faketime"]!="1780000000" or pin["ramdisk"]!="1" or
        pin["pace"]!="fast" or not re.fullmatch(r"default/[0-9A-F]{16}",pin["bram"]) or
        not re.fullmatch(r"[0-9]+/[0-9A-F]{16}",pin["rom"])):
        raise BisectError("missing or ineffective determinism/input pins")
    if (not re.fullmatch(r"[A-Z][A-Z0-9_]*",row.get("status","")) or
        not re.fullmatch(r"[0-9A-F]{16}",row.get("hash","")) or
        not re.fullmatch(r"[0-9A-F]{16}",row.get("run","")) or
        not re.fullmatch(r"[0-9]+",row.get("cycles",""))):raise BisectError("incomplete/malformed GSDIAG witness")
    witness={"status":row["status"],"hash":row["hash"],"cycles":int(row["cycles"]),"run":row["run"]}
    stable={k:v for k,v in pin.items() if k not in ("build","exe")}
    return witness,stable


def expand(argv,variables):
    result=[]
    for item in argv:
        for key,value in variables.items():item=item.replace("{"+key+"}",str(value))
        result.append(item)
    return result


def run_process(argv,cwd,env,timeout,log):
    start=time.monotonic()
    flags=subprocess.CREATE_NO_WINDOW if os.name=="nt" else 0
    try:
        with Path(log).open("wb") as stream:
            process=subprocess.Popen(argv,cwd=cwd,env=env,stdout=stream,stderr=subprocess.STDOUT,
                creationflags=flags,start_new_session=os.name!="nt")
            try:rc=process.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                if os.name=="nt":
                    subprocess.run(["taskkill","/PID",str(process.pid),"/T","/F"],capture_output=True)
                else:
                    import signal
                    os.killpg(process.pid,signal.SIGKILL)
                process.wait(timeout=10)
                raise BisectError(f"command timed out; log={log}")
    except OSError as exc:raise BisectError(f"cannot launch {argv[0]}: {exc}; log={log}") from exc
    return {"argv":argv,"returncode":rc,"seconds":round(time.monotonic()-start,3),"log":str(log)}


def extract(repo,revision,source,log):
    archive=source.parent/"source.tar"
    with archive.open("wb") as stream:
        result=subprocess.run(["git","-C",str(repo),"archive","--format=tar",revision],stdout=stream,stderr=subprocess.PIPE)
    if result.returncode:raise BisectError("git archive failed: "+result.stderr.decode(errors="replace"))
    source.mkdir()
    with tarfile.open(archive) as tar:
        members=tar.getmembers()
        for entry in members:
            relative(entry.name.rstrip("/"))
            if not (entry.isfile() or entry.isdir()):raise BisectError("source archive contains unsupported links/special entries")
        tar.extractall(source,members=members,filter="data")
    # Local detached metadata preserves configure-time revision identity. This is
    # not a registered worktree and has no remote/ref; writes stay in scratch.
    metadata=source/".git"
    (metadata/"objects/info").mkdir(parents=True);(metadata/"refs").mkdir()
    common=Path(text_git(repo,"rev-parse","--git-common-dir"))
    if not common.is_absolute():common=repo/common
    (metadata/"objects/info/alternates").write_bytes(((common.resolve()/"objects").as_posix()+"\n").encode())
    (metadata/"HEAD").write_bytes((revision+"\n").encode())
    fmt=text_git(repo,"rev-parse","--show-object-format")
    config="[core]\nrepositoryformatversion = "+("1" if fmt!="sha1" else "0")+"\nbare = false\n"
    if fmt!="sha1":config+="[extensions]\nobjectFormat = "+fmt+"\n"
    (metadata/"config").write_bytes(config.encode())
    git(source,"read-tree",revision)
    if text_git(source,"rev-parse","HEAD")!=revision:raise BisectError("scratch revision identity mismatch")
    archive.unlink() # exact file created above, no recursive cleanup


def execute(repo,before,after,scenario,scratch):
    repo=Path(repo).resolve();scenario=Path(scenario).resolve();scratch=Path(scratch).resolve()
    cfg=recipe(scenario)
    if scratch.exists() or inside(scratch,repo):raise BisectError("scratch must be a new directory outside the source repository")
    first=text_git(repo,"rev-parse","--verify","--end-of-options",before)
    last=text_git(repo,"rev-parse","--verify","--end-of-options",after)
    if any(text_git(repo,"cat-file","-t",oid)!="commit" for oid in (first,last)):
        raise BisectError("before and after must name commits")
    history=text_git(repo,"rev-list","--first-parent",last).splitlines()
    if first not in history:raise BisectError("before must be on after's first-parent history")
    revisions=list(reversed(history[:history.index(first)+1]))
    if len(revisions)>cfg["max_revisions"]:raise BisectError("range exceeds max_revisions")
    initial=source_fingerprint(repo)
    scratch.mkdir(parents=True)
    report={"schema":"a2bisect-report-v1","result":"inconclusive","repo":str(repo),"before":first,"after":last,
            "history_model":"first-parent; binary boundary plus exhaustive earlier-prefix verification",
            "scenario_sha256":sha(scenario),"scenario":cfg,"revisions":revisions,"observations":{},
            "dependencies":{},"inputs":[],"source_fingerprint_before":initial,"source_unchanged":False}
    (scratch/"scenario.json").write_text(json.dumps(cfg,indent=2))
    dependencies={};staged=[];stable_pins=None
    def save():
        pending=scratch/"report.pending"
        pending.write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
        pending.replace(scratch/"report.json")
    save()
    try:
        expected_links=links(repo,first)
        for index,(name,oid) in enumerate(sorted(expected_links.items())):
            path=repo/relative(name)
            if not path.is_dir() or not (path/".git").exists():raise BisectError(f"dependency is not initialized locally: {name}; no fetch is performed")
            actual=text_git(path,"rev-parse","HEAD")
            if actual!=oid:raise BisectError(f"dependency checkout differs from requested gitlink: {name}")
            target=scratch/"dependencies"/str(index)
            content=tree_snapshot(path,target)
            dependencies[name]=(path,target,content)
            report["dependencies"][name]={"gitlink":oid,"checkout_head":actual,**content,
                "model":"actual local bytes, including local changes, excluding Git metadata; fixed across revisions"}
        for index,item in enumerate(cfg.get("inputs",[])):
            src=Path(item["source"])
            if not src.is_absolute():src=scenario.parent/src
            src=src.resolve()
            if not src.is_file():raise BisectError(f"missing scenario input: {src}")
            target=scratch/"inputs"/str(index);target.parent.mkdir(exist_ok=True)
            shutil.copy2(src,target);digest=sha(target)
            staged.append((src,target,item,digest))
            report["inputs"].append({"source":str(src),"destination":item["destination"],"scope":item.get("scope","run"),"sha256":digest})
        save()
        def copy_input(target,root,dest):
            path=root/relative(dest)
            if not inside(path,root):raise BisectError("input destination escapes scratch")
            path.parent.mkdir(parents=True,exist_ok=True)
            if path.exists() and sha(path)!=sha(target):raise BisectError(f"input would replace different tracked/source content: {dest}")
            if not path.exists():shutil.copy2(target,path)
        def observe(index):
            nonlocal stable_pins
            revision=revisions[index]
            if revision in report["observations"]:return report["observations"][revision]["witness"]
            if links(repo,revision)!=expected_links:raise BisectError("dependency gitlinks change inside range; supply a range with fixed dependencies")
            root=scratch/"revisions"/revision;root.mkdir(parents=True)
            source=root/"source";build=root/"build"
            extract(repo,revision,source,root/"archive.log")
            for name,(_,snapshot,_) in dependencies.items():tree_snapshot(snapshot,source/relative(name))
            for _,target,item,_ in staged:
                if item.get("scope","run")=="source":copy_input(target,source,item["destination"])
            variables={"source":source,"build":build,"inputs":scratch/"inputs","repeat":0}
            env={k:v for k,v in os.environ.items() if not k.startswith(("A2GSPU_","MEMVU_","WIZ5_")) and k not in ("GSSQUARED_ROOT","A2_GSSQUARED","A2RAIL_DIR")}
            row={"revision":revision,"tree":text_git(repo,"show","-s","--format=%T",revision),"checks":[],"runs":[]}
            report["observations"][revision]=row;save()
            for label in ("configure","build"):
                event=run_process(expand(cfg[label],variables),root,env,cfg["build_timeout_seconds"],root/(label+".log"))
                row[label]=event;save()
                if event["returncode"]:raise BisectError(f"{label} failed for {revision}; log={event['log']}")
            exe=build/relative(cfg["executable"])
            if not exe.is_file():raise BisectError(f"built executable missing: {exe}")
            row["executable_sha256"]=sha(exe)
            for i,check in enumerate(cfg.get("checks",[])):
                check_env=dict(env,GSSQUARED_ROOT=str(source),A2_GSSQUARED=str(exe),A2RAIL_DIR=str(root/f"check-{i}"))
                event=run_process(expand(check,variables),root,check_env,cfg["check_timeout_seconds"],root/f"check-{i}.log")
                row["checks"].append(event);save()
                if event["returncode"]:raise BisectError(f"check failed for {revision}; log={event['log']}")
            for repeat in range(cfg["repeats"]):
                run=root/f"run-{repeat}";run.mkdir();variables.update(run=run,repeat=repeat)
                for _,target,item,_ in staged:
                    if item.get("scope","run")=="run":copy_input(target,run,item["destination"])
                run_env=dict(env,A2GSPU_SEED="1",A2GSPU_FAKETIME="1780000000",A2GSPU_BRAM="default",
                    A2GSPU_RAMDISK="1",A2GSPU_PACE="fast",A2GSPU_SPIKE_FRAMES=str(cfg["frames"]),
                    A2GSPU_OUT_DIR=str(run),A2GSPU_BACKGROUND_WINDOW="1")
                for key,value in cfg.get("env",{}).items():run_env[key]=expand([value],variables)[0]
                event=run_process([str(exe),*expand(cfg.get("arguments",[]),variables)],run,run_env,cfg["run_timeout_seconds"],run/"output.log")
                row["runs"].append(event);save()
                if event["returncode"] not in cfg["accepted_exit_codes"]:raise BisectError(f"scenario exited {event['returncode']}; log={event['log']}")
                witness,pins=parse_output((run/"output.log").read_text(encoding="utf-8",errors="replace"))
                if pins["frames"]!=str(cfg["frames"]):raise BisectError("frame pin differs from scenario")
                event.update(witness=witness,pins=pins);save()
                if stable_pins is None:stable_pins=pins
                if pins!=stable_pins:raise BisectError(f"determinism/input PINS differ at {revision}; log={event['log']}")
                if repeat and witness!=row["runs"][0]["witness"]:raise BisectError(f"nondeterministic witness at {revision}")
            row["witness"]=row["runs"][0]["witness"];save()
            print(f"observed {revision[:12]} {json.dumps(row['witness'],sort_keys=True)}",flush=True)
            return row["witness"]
        baseline=observe(0);endpoint=observe(len(revisions)-1)
        report["endpoint_equal"]=endpoint==baseline
        candidate=None
        if endpoint!=baseline:
            lo,hi=0,len(revisions)-1
            while hi-lo>1:
                mid=(lo+hi)//2
                if observe(mid)==baseline:lo=mid
                else:hi=mid
            candidate=hi
        # This check is what prevents reversions/non-monotonic histories from
        # turning matching endpoints into a false 'none', or a late boundary
        # into a false 'first'. A build/run failure is never a changed witness.
        stop=candidate if candidate is not None else len(revisions)-1
        first_change=next((i for i in range(1,stop+1) if observe(i)!=baseline),None)
        report["earlier_revisions_verified"]=True
        if first_change is None:report["result"]="none"
        else:
            report.update(result="change",first_changed=revisions[first_change],last_unchanged=revisions[first_change-1])
    except (BisectError,OSError,ValueError) as exc:
        report["result"]="inconclusive";report["error"]=str(exc)
        raise BisectError(str(exc)) from exc
    finally:
        report["source_fingerprint_after"]=source_fingerprint(repo)
        report["source_unchanged"]=report["source_fingerprint_after"]==initial
        changed=[]
        for name,(path,_,content) in dependencies.items():
            if tree_snapshot(path)!=content:changed.append(name)
        for src,_,_,digest in staged:
            if not src.is_file() or sha(src)!=digest:changed.append(str(src))
        report["dependency_inputs_unchanged"]=not changed
        if not report["source_unchanged"] or changed:
            report["result"]="inconclusive";report["error"]="source or dependency/input bytes changed during run"
            report.pop("first_changed",None);report.pop("last_unchanged",None)
        save()
    if report["result"]=="inconclusive":raise BisectError(report["error"])
    return report


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo",type=Path,default=Path(__file__).resolve().parents[2])
    parser.add_argument("--before");parser.add_argument("--after")
    parser.add_argument("--scenario",type=Path);parser.add_argument("--scratch",type=Path)
    parser.add_argument("--expect",choices=("change","none"))
    parser.add_argument("--selftest",action="store_true")
    args=parser.parse_args()
    if args.selftest:
        return subprocess.run([sys.executable,"-I",str(Path(__file__).with_name("bisectcheck.py"))]).returncode
    if not all((args.before,args.after,args.scenario,args.scratch)):parser.error("--before, --after, --scenario and --scratch are required")
    try:
        result=execute(args.repo,args.before,args.after,args.scenario,args.scratch)
        print(json.dumps({k:result[k] for k in ("result","first_changed","last_unchanged","source_unchanged") if k in result},indent=2))
        return 1 if args.expect and result["result"]!=args.expect else 0
    except (BisectError,OSError) as exc:print(f"bisect: INCONCLUSIVE {exc}",file=sys.stderr);return 2

if __name__=="__main__":
    sys.exit(main())
