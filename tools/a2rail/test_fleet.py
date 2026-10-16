"""Fleet shape/refusal fixtures; private projects and emulator are unnecessary."""
import copy
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parent))
import fleet


class FleetPlanTests(unittest.TestCase):
    def plan(self):
        return {'schema':'a2fleet-plan-v1','workers':2,'jobs':[
            {'id':'p3-seed1','platform':3,'seed':1,'timeout_seconds':30,'files':[],
             'commands':['poke 004000 42'],'assertions':['peek:004000 == $42']}]}

    def test_explicit_cell_passes(self):fleet.validate(self.plan())

    def test_missing_assertions_refuses(self):
        plan=self.plan();plan['jobs'][0]['assertions']=[]
        with self.assertRaises(ValueError):fleet.validate(plan)

    def test_duplicate_or_escaping_ids_refuse(self):
        plan=self.plan();plan['jobs'].append(copy.deepcopy(plan['jobs'][0]))
        with self.assertRaises(ValueError):fleet.validate(plan)
        plan=self.plan();plan['jobs'][0]['id']='../outside'
        with self.assertRaises(ValueError):fleet.validate(plan)

    def test_injection_unknown_fields_and_invalid_bounds_refuse(self):
        for key,value in [('seed',True),('timeout_seconds',0),('commands',['cpu\nreset']),('unknown',1)]:
            plan=self.plan();plan['jobs'][0][key]=value
            with self.assertRaises(ValueError,msg=key):fleet.validate(plan)

    def test_undeclared_input_and_escaping_output_refuse(self):
        for command in ['load 800 {input:missing.bin}','context snapshot {output:../outside.json}']:
            plan=self.plan();plan['jobs'][0]['commands']=[command]
            with self.assertRaises(ValueError):fleet.validate(plan)

    def test_platform_and_config_are_exclusive(self):
        plan=self.plan();plan['jobs'][0]['config']='machine.gs2'
        with self.assertRaises(ValueError):fleet.validate(plan)

    def test_native_key_tokens_remain_native_syntax(self):
        plan=self.plan();plan['jobs'][0]['commands']=['keys {RETURN}']
        fleet.validate(plan)


if __name__=='__main__':unittest.main(verbosity=2)
