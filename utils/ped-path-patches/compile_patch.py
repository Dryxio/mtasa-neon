#!/usr/bin/env python3
"""Compile explicit pedestrian graph edits; never distribute vanilla NODES data."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct

HEADER = struct.Struct('<5I')
NODE_SIZE = 28
DYNAMIC = 192
MAX_NODES = 4096
MAX_LINKS = 32768

class Area:
    def __init__(self, area, data):
        self.id = area
        if len(data) < 20:
            raise ValueError('truncated NODES header')
        self.count, self.vehicles, self.peds, self.cars, self.addresses = HEADER.unpack_from(data)
        if self.count != self.vehicles + self.peds or self.count > MAX_NODES or self.addresses > MAX_LINKS-DYNAMIC or self.cars > 65535:
            raise ValueError('unsupported NODES counts')
        nlinks = self.addresses + DYNAMIC if self.addresses else 0
        size = 20 + self.count*28 + self.cars*14 + nlinks*6 + self.addresses*2
        if len(data) < size:
            raise ValueError('truncated NODES arrays')
        offset = 20
        self.nodes = [bytearray(data[offset+i*28:offset+(i+1)*28]) for i in range(self.count)]
        offset += self.count*28
        car_data = data[offset:offset+self.cars*14]; offset += self.cars*14
        link_data = data[offset:offset+nlinks*4]; offset += nlinks*4
        navi_data = data[offset:offset+self.addresses*2]; offset += self.addresses*2
        lengths = data[offset:offset+nlinks]; offset += nlinks
        flags = data[offset:offset+nlinks]
        self.links = list(struct.iter_unpack('<HH', link_data))
        self.lengths, self.flags = lengths, flags
        normalized = bytearray(data[:20])
        for i, node in enumerate(self.nodes):
            base, aid, nid = struct.unpack_from('<HHH', node, 16)
            if aid != area or nid != i or base+(node[24]&15) > self.addresses:
                raise ValueError('invalid node identity or link range')
            node = node.copy()
            node[:8] = bytes(8); node[14:16] = bytes(2)
            node[24] &= ~0x20; node[25] &= ~7
            normalized.extend(node)
        normalized.extend(car_data)
        normalized.extend(link_data[:self.addresses*4])
        normalized.extend(navi_data)
        normalized.extend(lengths[:self.addresses]); normalized.extend(flags[:self.addresses])
        self.digest = hashlib.sha256(normalized).digest()

    def edit(self, node_id):
        n = self.nodes[node_id]
        base = struct.unpack_from('<H', n, 16)[0]
        return dict(id=node_id, position=[v/8 for v in struct.unpack_from('<3h', n, 8)], width=n[22], flood=n[23],
                    enabled=True, links=[dict(area=self.links[k][0], node=self.links[k][1], length=self.lengths[k], flags=self.flags[k])
                                       for k in range(base, base+(n[24]&15))])

def integer(value, lo, hi, label):
    if isinstance(value, bool) or not isinstance(value, int) or not lo <= value <= hi:
        raise ValueError(f'invalid {label}')
    return value

def compile_patch(areas, spec):
    edits = {}
    for item in spec['areas']:
        a = integer(item['area'], 0, 63, 'area')
        if a in edits: raise ValueError('duplicate area')
        edits[a] = {}
        for edit in item['nodes']:
            node = integer(edit['id'], areas[a].vehicles, MAX_NODES-1, 'ped node')
            if node in edits[a]: raise ValueError('duplicate node')
            edits[a][node] = edit
    if not any(edits.values()): raise ValueError('empty patch')
    deleted = {(a,n) for a, es in edits.items() for n,e in es.items() if not e.get('enabled', True)}
    if any(n >= areas[a].count for a,n in deleted): raise ValueError('cannot delete an added node')
    # Include every affected predecessor when removing nodes. Native tasks can
    # still hold an old address, but new routes must not reach a tombstone.
    if deleted:
        for a, base in areas.items():
            for n in range(base.count):
                e = edits.get(a, {}).get(n, base.edit(n))
                if any((l['area'],l['node']) in deleted for l in e['links']):
                    if n < base.vehicles: raise ValueError('deletion would modify vehicle data')
                    e = dict(e, links=[l for l in e['links'] if (l['area'],l['node']) not in deleted])
                    edits.setdefault(a,{})[n] = e
    # GTA's reverse distance propagation requires equal-cost reciprocal edges.
    # Inspect every original predecessor, not only the replacement edges: deleting
    # one direction of a vanilla connection otherwise passes local validation.
    graph = {a:{n:base.edit(n) for n in range(base.vehicles,base.count)} for a,base in areas.items()}
    for a,es in edits.items(): graph[a].update(es)
    for a,nodes in graph.items():
        for n,e in nodes.items():
            for link in e['links']:
                ta,tn = link['area'],link['node']
                target=graph.get(ta,{}).get(tn)
                if target is None or not target.get('enabled',True): raise ValueError('missing or disabled target')
                if not any(back['area']==a and back['node']==n and back['length']==link['length'] for back in target['links']):
                    raise ValueError('asymmetric pedestrian link')
    scope = set(range(64))
    out = bytearray(b'NPEDPAT1' + struct.pack('<I', len(scope)))
    for a in sorted(scope):
        base = areas[a]; es = edits.get(a,{})
        next_id = base.count; budget = base.addresses
        out.extend(struct.pack('<5I', a, base.count, base.vehicles, base.addresses, base.cars)+base.digest+struct.pack('<I',len(es)))
        for n,e in sorted(es.items()):
            if n >= base.count:
                if n != next_id: raise ValueError('added nodes must be contiguous')
                next_id += 1
            if not isinstance(e.get('enabled',True),bool): raise ValueError('enabled must be boolean')
            enabled = e.get('enabled',True)
            links = e['links']
            if len(links)>15 or (not enabled and links): raise ValueError('invalid link count')
            pos = e['position']
            if len(pos)!=3 or any(isinstance(v,bool) or not isinstance(v,(int,float)) or not math.isfinite(v) or not -4096<=v<=4095.875 for v in pos):
                raise ValueError('position outside native fixed-point range')
            q = [round(v*8) for v in pos]
            flood = integer(e['flood'],0,255,'flood')
            out.extend(struct.pack('<H3h4B', n,*q,integer(e['width'],0,255,'width'),flood,enabled,len(links)))
            seen = set()
            for l in links:
                target_a = integer(l['area'],0,63,'target area')
                target_n = integer(l['node'],areas[target_a].vehicles,MAX_NODES-1,'target node')
                key = target_a,target_n
                if key in seen or key==(a,n): raise ValueError('duplicate or self link')
                seen.add(key)
                target = edits.get(target_a,{}).get(target_n)
                if target is None:
                    if target_n >= areas[target_a].count: raise ValueError('missing target')
                    target = areas[target_a].edit(target_n)
                if not target.get('enabled',True) or target['flood'] != flood: raise ValueError('disabled target or flood mismatch')
                out.extend(struct.pack('<HHBB',target_a,target_n,integer(l['length'],1,255,'length'),integer(l.get('flags',0),0,3,'flags')))
            budget += len(links)
            if budget>MAX_LINKS-DYNAMIC: raise ValueError('too many links')
    if len(out)>4*1024*1024: raise ValueError('patch too large')
    return bytes(out)

def load_areas(directory):
    return {a:Area(a,(directory/f'NODES{a}.DAT').read_bytes()) for a in range(64)}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--paths',type=Path,required=True)
    p.add_argument('--spec',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    args=p.parse_args()
    data=compile_patch(load_areas(args.paths),json.loads(args.spec.read_text()))
    args.output.write_bytes(data)
    print(json.dumps(dict(revision=hashlib.sha256(data).hexdigest(),bytes=len(data),output=str(args.output))))

if __name__=='__main__': main()
