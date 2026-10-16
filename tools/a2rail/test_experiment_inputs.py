"""Declared data/preservation observations for primitive experiments."""
import copy
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parent))
import experiment


class InputPlanTests(unittest.TestCase):
    def plan(self):
        return {'schema':'a2experiment-plan-v1','workload':{'id':'copy','case':'four'},'platform':3,
                'files':[{'source':str(Path(__file__).resolve()),'path':'input.bin','sha256':'0'*64}],
                'build':[],'image':{'path':'program.bin','address':2048,'format':'raw'},
                'entry':2048,'target':2060,'attempts':100,'registers':{'e':1,'p':52,'x':165},
                'loads':[{'path':'input.bin','address':4096}],
                'checks':[{'address':16384,'bytes':[1,2,3,4]},
                          {'address':4096,'bytes':[1,2,3,4],'role':'preserved-input'}],
                'preserve_registers':['x'],'profile':True,
                'footprint':{'windows':[{'bank':0,'lo':0,'hi':65535}],
                             'allowed':[{'address':16384,'length':4}],'max_records':8}}

    def test_declared_input_and_preservation_plan(self):
        experiment.validate(self.plan())

    def test_unknown_memory_check_role_refuses(self):
        plan=self.plan();plan['checks'][1]['role']='plausible'
        with self.assertRaises(ValueError):experiment.validate(plan)

    def test_output_must_have_a_negative_control_target(self):
        plan=self.plan();plan['checks']=[plan['checks'][1]]
        with self.assertRaises(ValueError):experiment.validate(plan)

    def test_bad_observer_shapes_refuse(self):
        for key,value in [('profile',1),('preserve_registers',['unknown']),
                          ('loads',[{'path':'input.bin','address':True}]),
                          ('footprint',{'windows':[],'allowed':[],'max_records':0})]:
            plan=self.plan();plan[key]=value
            with self.assertRaises(ValueError,msg=key):experiment.validate(plan)

    def test_complete_write_footprint(self):
        policy=self.plan()['footprint']
        records=[{'event':'watch.write','kind':'write','pc':2048,'addr':16384+i,'data':i} for i in range(4)]
        value=experiment.check_footprint(records,policy)
        self.assertTrue(value['ok'])
        self.assertEqual(value['writes'],4)
        self.assertEqual(value['writers'][0]['pc'],2048)

    def test_unexpected_write_and_loss_fail(self):
        policy=self.plan()['footprint']
        for records in [[{'event':'watch.write','kind':'write','pc':2048,'addr':2,'data':0}],
                        [{'event':'watch.suppressed'}]]:
            value=experiment.check_footprint(records,policy)
            self.assertFalse(value['ok'])

    def test_profile_totals_must_cover_measured_interval(self):
        profile={'schema':'a2profile-v1','model':'instruction-cycles','total_instructions':2,
                 'total_cycles':6,'pcs':[{'pc':2048,'instructions':2,'cycles':6}]}
        self.assertTrue(experiment.check_profile(profile,{'instructions':'2','cycles':'6'})['ok'])
        bad=copy.deepcopy(profile);bad['pcs'][0]['cycles']=5
        self.assertFalse(experiment.check_profile(bad,{'instructions':'2','cycles':'6'})['ok'])


if __name__=='__main__':unittest.main(verbosity=2)
