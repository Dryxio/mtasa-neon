import copy
import importlib.util
from pathlib import Path
import struct
import unittest

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('compile_patch', ROOT/'utils/ped-path-patches/compile_patch.py')
compiler = importlib.util.module_from_spec(spec)
spec.loader.exec_module(compiler)

def raw_area(area=0):
    nodes=[]
    for i in range(3):
        n=bytearray(28)
        struct.pack_into('<3h',n,8,i*8,0,0)
        struct.pack_into('<4H',n,14,32766,i,area,i)
        n[22:]=bytes([16,5,1,0,15,0])
        nodes.append(n)
    links=struct.pack('<6H',area,0,area,2,area,1)+bytes(192*4)
    return struct.pack('<5I',3,1,2,0,3)+b''.join(nodes)+links+bytes(3*2)+bytes([1,1,1])+bytes(192)+bytes(3+192)

class CompilerTests(unittest.TestCase):
    def setUp(self):
        self.areas={a:compiler.Area(a,raw_area(a)) for a in range(64)}
        self.edit=self.areas[0].edit(1)
        self.edit['position']=[1,2,3]
        self.spec={'areas':[{'area':0,'nodes':[self.edit]}]}

    def test_deterministic_and_geometry_changes_revision(self):
        a=compiler.compile_patch(self.areas,self.spec)
        self.assertEqual(a,compiler.compile_patch(self.areas,copy.deepcopy(self.spec)))
        self.edit['position'][0]=2
        self.assertNotEqual(a,compiler.compile_patch(self.areas,self.spec))
        self.assertEqual(a[:8],b'NPEDPAT1')

    def test_normalization_ignores_only_runtime_search_fields(self):
        raw=bytearray(raw_area())
        raw[20:28]=b'abcdefgh'; raw[34:36]=b'ab'; raw[44]|=0x20; raw[45]|=7
        self.assertEqual(compiler.Area(0,raw).digest,self.areas[0].digest)
        raw[28]^=1
        self.assertNotEqual(compiler.Area(0,raw).digest,self.areas[0].digest)

    def test_reject_vehicle_edit(self):
        self.edit['id']=0
        with self.assertRaises(ValueError): compiler.compile_patch(self.areas,self.spec)

    def test_reject_nan_and_out_of_range(self):
        for value in [float('nan'),float('inf'),4096,-4097,True]:
            self.edit['position'][0]=value
            with self.assertRaises(ValueError): compiler.compile_patch(self.areas,self.spec)

    def test_reject_missing_target_and_flood(self):
        for node in [0,3,65535]:
            self.edit['links'][0]['node']=node
            with self.assertRaises(ValueError): compiler.compile_patch(self.areas,self.spec)
        self.edit['links'][0]['node']=2
        self.edit['flood']=6
        with self.assertRaises(ValueError): compiler.compile_patch(self.areas,self.spec)

    def test_deletion_removes_incoming_and_validates_all_regions(self):
        self.edit.update(enabled=False,links=[])
        data=compiler.compile_patch(self.areas,self.spec)
        self.assertEqual(struct.unpack_from('<I',data,8)[0],64)

    def test_cross_region_dependencies(self):
        self.edit['links'][0]['area']=1
        other=self.areas[1].edit(2)
        other['links'][0]['area']=0
        # Also repair the two old counterparts.
        old=self.areas[0].edit(2); old['links'][0]['area']=1
        other_old=self.areas[1].edit(1); other_old['links'][0]['area']=0
        self.spec['areas'][0]['nodes'].append(old)
        self.spec['areas'].append({'area':1,'nodes':[other,other_old]})
        data=compiler.compile_patch(self.areas,self.spec)
        self.assertEqual(struct.unpack_from('<I',data,8)[0],64)

    def test_reject_asymmetric_or_unequal_cost(self):
        self.edit['links']=[]
        with self.assertRaisesRegex(ValueError, 'asymmetric'): compiler.compile_patch(self.areas,self.spec)
        self.edit['links']=[dict(area=0,node=2,length=2)]
        with self.assertRaisesRegex(ValueError, 'asymmetric'): compiler.compile_patch(self.areas,self.spec)

    def test_truncated_base(self):
        for size in [0,19,20,len(raw_area())-1]:
            with self.assertRaises(ValueError): compiler.Area(0,raw_area()[:size])

    def test_reject_noncontiguous_append_and_duplicate(self):
        self.edit['id']=4
        with self.assertRaises(ValueError): compiler.compile_patch(self.areas,self.spec)
        self.edit['id']=3
        self.edit['links']=[]
        compiler.compile_patch(self.areas,self.spec)
        self.spec['areas'][0]['nodes'].append(self.edit)
        with self.assertRaises(ValueError): compiler.compile_patch(self.areas,self.spec)

    def test_reject_link_flags_cost_duplicate(self):
        for field,value in [('flags',4),('length',0),('length',256)]:
            e=copy.deepcopy(self.spec);e['areas'][0]['nodes'][0]['links'][0][field]=value
            with self.assertRaises(ValueError): compiler.compile_patch(self.areas,e)
        self.edit['links']*=2
        with self.assertRaises(ValueError): compiler.compile_patch(self.areas,self.spec)

if __name__=='__main__': unittest.main()
