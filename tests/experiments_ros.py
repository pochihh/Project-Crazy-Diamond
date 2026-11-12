#!/usr/bin/env python3
"""ROS-only experiment acceptance; UI can be absent. Always ends disabled."""
import argparse
import csv
import io
import json
import math
from pathlib import Path
import time
import rclpy
from rclpy.node import Node
from phantom_interfaces.srv import Command as PhantomCommand
from interfaces.msg import BridgeState, CommandResult, PositionCommand
from interfaces.srv import Command, LoadCsv, ListPrograms, RunnerCommand, LoggerCommand


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--physical',action='store_true');args=parser.parse_args()
    rclpy.init();node=Node('experiment_acceptance');state=None;results={}
    def update(s):
        nonlocal state;state=s
    node.create_subscription(BridgeState,'/bridge/state',update,1)
    node.create_subscription(CommandResult,'/bridge/command_result',lambda m:results.__setitem__(m.command_id,m),256)
    clients={name:node.create_client(kind,path) for name,kind,path in [
        ('host',Command,'/bridge/command'),('csv',LoadCsv,'/data/load_csv'),('files',ListPrograms,'/data/programs'),
        ('runner',RunnerCommand,'/data/runner'),('logger',LoggerCommand,'/data/logger'),('phantom',PhantomCommand,'/phantom/command')]}
    pub=node.create_publisher(PositionCommand,'/bridge/target',16)
    def wait(condition,seconds=15):
        deadline=time.monotonic()+seconds
        while time.monotonic()<deadline:
            rclpy.spin_once(node,timeout_sec=.01)
            if condition():return
        raise AssertionError('Timed out waiting for state')
    def call(name,req,check=True):
        client=clients[name];assert client.wait_for_service(timeout_sec=10),name
        f=client.call_async(req);wait(f.done,30);out=f.result()
        if hasattr(out,'accepted'):
            if check:assert out.accepted,out.message
            if out.accepted and getattr(out,'command_id',0):
                wait(lambda:out.command_id in results and state.last_command_id>=out.command_id)
                if check:assert results[out.command_id].applied,results[out.command_id].message
        return out
    wait(lambda:state is not None and state.connected)
    assert state.mock!=args.physical and not state.recording and not state.system_enabled
    baseline=(state.telemetry_errors,state.deadline_misses,state.dropped_samples)
    profiles=[c.id for c in state.controllers[:2]]
    signal_names=['axis0_target_mm','axis1_target_mm','axis0_reference_mm','axis1_reference_mm','axis0_position_mm','axis1_position_mm','axis0_output_duty','axis1_output_duty','do0_command','do0','runner_row','runner_cycles']
    def csv_rows():
        wait(lambda:not state.recording)
        path=Path('recordings')/state.recording_file
        with path.open() as f:rows=list(csv.DictReader(line for line in f if not line.startswith('#')))
        assert rows and list(rows[0])==['timestamp_us','sample']+signal_names
        for a,b in zip(rows,rows[1:]):assert int(b['sample'])==int(a['sample'])+1
        assert int(rows[0]['runner_row'])==0
        return rows
    reports={}
    try:
        call('host',Command.Request(operation='clear'))
        for a in range(2):
            call('host',Command.Request(operation='profile_load',axis=a,profile='research_pid'))
        content='axis0_reference_mm,axis1_reference_mm,do0,note\n'
        for tick in range(500):
            angle=2*math.pi*tick/500
            content+=f'{.3*math.sin(angle)},{.3*math.sin(angle+math.pi/2)},{int(tick<10)},ignored\n'
        loaded=call('csv',LoadCsv.Request(name='acceptance-wave.csv',csv=content,overwrite=True))
        assert len(loaded.warnings)==1 and loaded.rows==500
        assert 'acceptance-wave.csv' in call('files',ListPrograms.Request()).names
        for completion in ['loop','hold','stop']:
            call('runner',RunnerCommand.Request(operation='start',completion=completion,auto_enable=True,record=True,signals=signal_names))
            if completion=='loop':
                rejections=state.input_rejections;pub.publish(PositionCommand(axis=0,position_mm=7.0))
                wait(lambda:state.input_rejections>rejections)
                wait(lambda:state.runner_cycles>=3)
                call('runner',RunnerCommand.Request(operation='stop'))
            elif completion=='hold':
                wait(lambda:state.runner_state==2);assert state.system_enabled and state.recording
                rejected=call('csv',LoadCsv.Request(name='acceptance-wave.csv'),False);assert not rejected.accepted
                call('runner',RunnerCommand.Request(operation='stop'))
            else:wait(lambda:state.runner_state==3 and not state.recording)
            rows=csv_rows()
            for row in rows:
                tick=int(row['runner_row']);assert int(row['do0_command'])==int(tick<10)
                assert float(row['axis0_target_mm'])==float(row['axis0_reference_mm'])
                assert float(row['axis1_target_mm'])==float(row['axis1_reference_mm'])
            if completion=='stop':assert len(rows)==500,len(rows)
            reports[completion]={'samples':len(rows),'recording':state.recording_file}
            assert state.dsp.arm_mask==0
        for source in ['sine','csv']:
            call('phantom',PhantomCommand.Request(operation='start',source=source,period_s=.25,
                 amplitude_mm=[.3,.3],offset_mm=[0.,0.],phase_deg=180.,pulse_width_ms=5.,
                 completion='loop',filename='acceptance-wave.csv',signals=signal_names))
            wait(lambda:state.runner_cycles>=2)
            call('phantom',PhantomCommand.Request(operation='stop'))
            rows=csv_rows();reports['phantom_'+source]={'samples':len(rows),'recording':state.recording_file}
        call('logger',LoggerCommand.Request(operation='start',signals=['axis0_position_mm','do0']))
        assert not call('logger',LoggerCommand.Request(operation='start',signals=['do0']),False).accepted
        call('logger',LoggerCommand.Request(operation='stop'))
        wait(lambda:not state.recording)
        assert (state.telemetry_errors,state.dropped_samples)==(baseline[0],baseline[2])
        if args.physical:assert state.deadline_misses==baseline[1]
        print(json.dumps({'result':'PASS','physical':args.physical,'checks':reports,'max_cycle_us':state.max_cycle_us,
              'telemetry_errors':state.telemetry_errors,'deadline_misses':state.deadline_misses,'dropped_samples':state.dropped_samples},indent=2))
    finally:
        call('runner',RunnerCommand.Request(operation='stop'),False)
        call('host',Command.Request(operation='stop'),False)
        call('logger',LoggerCommand.Request(operation='stop'),False)
        for a,profile in enumerate(profiles):call('host',Command.Request(operation='profile_load',axis=a,profile=profile),False)
        node.destroy_node();rclpy.shutdown()

if __name__=='__main__':main()
