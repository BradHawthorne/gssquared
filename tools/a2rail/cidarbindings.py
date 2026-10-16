"""Digest joins for owned CIDAR experiments, not a historical-fidelity oracle.

The raw-load mapping is deliberately narrow: absolute labels in Rosetta's
house debug text, a single raw image, and explicit data loads in the contract.
OMF relocation/OS-loader acceptance needs its own load model.
"""
import hashlib
import json
from pathlib import Path
import re


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def relative(root, name):
    if not isinstance(name,str) or not name or Path(name).is_absolute() or ':' in name or '..' in Path(name).parts:
        raise ValueError('CIDAR artifact must be relative')
    path=(root/name).resolve()
    if not path.is_relative_to(root):raise ValueError('CIDAR artifact escapes run')
    return path


def validate_declaration(value):
    if not isinstance(value,dict) or set(value)!={'spec','candidate','loadmap'}:
        raise ValueError('CIDAR declaration requires spec/candidate/loadmap')
    for name in value.values():relative(Path.cwd().resolve(),name)


def verify(root, declaration, pinned, contract, image, entry, target):
    """Refuse a substituted spec, source, image, debug map or build receipt."""
    validate_declaration(declaration)
    root=Path(root).resolve();artifacts={}
    def retained(name, expected=None):
        path=relative(root,name);actual=digest(path)
        if pinned.get(name)!=actual or (expected is not None and actual!=expected):
            raise ValueError('CIDAR artifact binding mismatch: '+name)
        artifacts[name]=actual
        return path
    def read(name, schema):
        value=json.loads(retained(name).read_text(encoding='utf-8'))
        if not isinstance(value,dict) or value.get('schema')!=schema:
            raise ValueError('unsupported CIDAR schema: '+schema)
        return value
    spec=read(declaration['spec'],'cidar-primitive-spec-v1')
    candidate=read(declaration['candidate'],'cidar-candidate-v1')
    mapping=read(declaration['loadmap'],'cidar-raw-loadmap-v1')
    for value in (spec,candidate):
        if not isinstance(value.get('id'),str) or not value['id'] or type(value.get('version')) is not int or value['version']<1:
            raise ValueError('CIDAR identities require id and positive version')
    if spec.get('contract')!=contract or not isinstance(spec.get('lineage'),dict) or not spec['lineage']:
        raise ValueError('CIDAR spec contract/lineage mismatch')
    if candidate.get('spec_sha256')!=artifacts[declaration['spec']] or candidate.get('loadmap_sha256')!=artifacts[declaration['loadmap']] or candidate.get('image_sha256')!=image['sha256']:
        raise ValueError('CIDAR spec/map/image identity mismatch')
    if any(mapping.get(key)!=image[key] for key in ('address','size')) or mapping.get('image_sha256')!=image['sha256']:
        raise ValueError('CIDAR raw load range/image mismatch')
    debug=mapping['debug'];debugpath=retained(debug['path'],debug['sha256'])
    symbols={}
    for name,address in re.findall(r'^\s*symbol "([^"\r\n]+)" type LABEL segment "~MAIN" segnum \d+ addr \$([0-9A-Fa-f]+)\b',debugpath.read_text(encoding='utf-8'),re.MULTILINE):
        if name in symbols:raise ValueError('ambiguous CIDAR raw debug symbol')
        symbols[name]=int(address,16)
    for key,address in [('entry',entry),('target',target)]:
        row=mapping[key]
        if row['address']!=address or symbols.get(row['symbol'])!=address or not image['address']<=address<image['address']+image['size']:
            raise ValueError('CIDAR raw debug/load '+key+' mismatch')
    sources=candidate.get('sources')
    if not isinstance(sources,list) or not 1<=len(sources)<=32:raise ValueError('CIDAR sources required')
    for row in sources:
        if set(row)!={'path','sha256','role'} or row['role'] not in ('owned-reference-invocation','owned-library','owned-candidate'):
            raise ValueError('CIDAR source requires explicit owned role')
        retained(row['path'],row['sha256'])
    build=candidate['build'];retained(build['path'],build['sha256'])
    receipt=read(build['path'],'cidar-build-receipt-v1')
    if receipt.get('exit_code')!=0 or receipt.get('inputs')!=sources or not isinstance(receipt.get('argv'),list) or not receipt['argv']:
        raise ValueError('CIDAR successful build inputs/argv mismatch')
    tool=receipt['tool']
    if not Path(tool['path']).is_absolute() or receipt['argv'][0]!=tool['path'] or digest(tool['path'])!=tool['sha256']:
        raise ValueError('CIDAR build tool identity mismatch')
    outputs=receipt.get('outputs',[])
    if not isinstance(outputs,list) or not 2<=len(outputs)<=8:raise ValueError('CIDAR build outputs required')
    for row in outputs:retained(row['path'],row['sha256'])
    if image['sha256'] not in [row['sha256'] for row in outputs] or debug['sha256'] not in [row['sha256'] for row in outputs]:
        raise ValueError('CIDAR build omitted image/debug identity')
    return {'schema':'cidar-experiment-binding-v1','primitive':{'id':spec['id'],'version':spec['version'],'sha256':artifacts[declaration['spec']]},
            'candidate':{'id':candidate['id'],'version':candidate['version'],'sha256':artifacts[declaration['candidate']]},
            'lineage':spec['lineage'],'declaration':declaration,'artifacts':artifacts,'build_tool':tool,
            'model':'local digest-bound owned sources/build/raw absolute debug mapping; declared historical lineage is not independently authenticated'}
