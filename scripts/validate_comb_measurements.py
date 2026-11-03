"""Independent stdlib comb recurrence/transfer and raw CPU audit. Never approves listening."""
import argparse
import array
import cmath
import csv
import hashlib
import json
import math
from pathlib import Path
import sys

RATES=(8000,44100,48000,96000,192000,384000)
def require(ok,message):
    if not ok: raise ValueError(message)
def digest(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def finite_tree(value):
    if isinstance(value,float): require(math.isfinite(value),'nonfinite JSON')
    elif isinstance(value,dict):
        for x in value.values(): finite_tree(x)
    elif isinstance(value,list):
        for x in value: finite_tree(x)
def checked_file(root,name,identity):
    path=(root/name).resolve()
    require(path.is_relative_to(root.resolve()),'path escapes artifact directory')
    require(digest(path)==identity,'hash mismatch: '+name)
    return path
def coefficient(rate,s):
    return math.exp(-2*math.pi*min(s['dampingHz'],.45*rate)/rate) if s['dampingEnabled'] else 0.
def transfer(rate,s,hz):
    z=cmath.exp(-2j*math.pi*hz/rate);delay=rate*s['delayMs']*.001;d=math.floor(delay);t=delay-d
    read=z**d*((1-t)+t*z);a=coefficient(rate,s);low=(1-a)/(1-a*z)
    wet=(1+s['feedforward']*read)/(1-s['feedback']*read*low)
    return 1+ s['mix']*(wet-1) if not s['bypass'] else 1+0j
def decay_rate(rate,s):
    # Positive comparison system dominates signed feedback and interpolation.
    delay=math.ceil(rate*s['delayMs']*.001);g=abs(s['feedback']);a=coefficient(rate,s)
    if g==0: return a
    lo=max(a,1e-12);hi=1.
    for _ in range(80):
        q=(lo+hi)/2
        # The first trials can be far below the very slow pole at large D.
        # Compare the positive characteristic in log space to avoid q**-D
        # overflowing even though the final pole and residual are representable.
        characteristic_log=math.log(g)+math.log1p(-a)-math.log1p(-a/q)-delay*math.log(q)
        if characteristic_log>0:lo=q
        else:hi=q
    return hi
def tail_bound(rate,s):
    q=decay_rate(rate,s)
    return max(s['delayMs']*.001,math.log(1e-9)/math.log(q)/rate) if q>0 else s['delayMs']*.001
def reference(source,rate,s):
    delay=rate*s['delayMs']*.001;integer=math.floor(delay);fraction=delay-integer
    ring=[0.]*(math.ceil(delay)+4);pos=0;low=0.;a=coefficient(rate,s);out=[]
    for x in source:
        d=(1-fraction)*ring[(pos-integer)%len(ring)]+fraction*ring[(pos-integer-1)%len(ring)]
        low=(1-a)*d+a*low;value=x+s['feedback']*low;ring[pos]=value;pos=(pos+1)%len(ring)
        y=x+s['mix']*(value+s['feedforward']*d-x)
        out.append(x if s['bypass'] else y)
    return out
def compare_impulse(actual,expected):
    require(len(actual)==len(expected),'impulse length mismatch')
    err=max((abs(x-y) for x,y in zip(actual,expected)),default=0)
    require(err<=2e-6,'impulse recurrence mismatch')
    return err
def reference_automation(source,rate,initial,events):
    def values(s):return [rate*s['delayMs']*.001,s['feedback'],s['feedforward'],coefficient(rate,s),s['mix'],float(s['bypass'])]
    current=values(initial);target=current[:];steps=[0.]*6;remaining=[0]*6
    ring=[0.]*(math.ceil(.04*rate)+4);position=0;low=0.;out=[];schedule={e['frame']:e['settings'] for e in events}
    for n,x in enumerate(source):
        if n in schedule:
            proposed=values(schedule[n])
            for k,t in enumerate(proposed):
                if t!=target[k]:
                    target[k]=t;remaining[k]=math.floor(rate*(.1 if k==0 else .01));steps[k]=(t-current[k])/remaining[k]
        for k in range(6):
            if remaining[k]>0:
                remaining[k]-=1;current[k]=target[k] if remaining[k]==0 else current[k]+steps[k]
        delay,g,f,a,mix,bypass=current;integer=math.floor(delay);fraction=delay-integer
        d=(1-fraction)*ring[(position-integer)%len(ring)]+fraction*ring[(position-integer-1)%len(ring)]
        low=(1-a)*d+a*low;v=x+g*low;ring[position]=v;position=(position+1)%len(ring)
        wet=x+mix*(v+f*d-x);out.append(wet+bypass*(x-wet))
    return out
def load_float(path):
    data=array.array('f');data.frombytes(path.read_bytes())
    if sys.byteorder!='little':data.byteswap()
    require(all(math.isfinite(x) for x in data),'nonfinite capture')
    return data
def audit_measurements(root,report):
    cases=report['cases'];require(len(cases)==42,'incomplete measurement matrix')
    require({(c['rate'],c['scenario']) for c in cases}=={(r,s) for r in RATES for s in range(7)},'incomplete measurement matrix')
    results=[]
    for c in cases:
        rate,s=c['rate'],c['settings'];data=load_float(checked_file(root,c['file'],c['sha256']))
        require(len(data)==c['frames'] and c['latency_samples']==0,'capture length/latency mismatch')
        require(c['irregular_repeat_bit_exact'],'repeat determinism failed')
        # Compare every captured output sample against independently implemented recurrence.
        source=[0.]*len(data);source[0]=1.;error=compare_impulse(data,reference(source,rate,s))
        require(len(data)/rate>=s['delayMs']*.001,'short impulse capture')
        q=decay_rate(rate,s)
        if s['feedback']:
            envelope=(1+abs(s['feedforward'])*q**(-math.ceil(rate*s['delayMs']*.001)))*q**math.floor(c['tail_seconds']*rate)
            require(envelope<=1.0001e-6,'reported tail is not conservative')
        # Weighted-state envelope gives a conservative remaining impulse l1 sum.
        residual=0. if q==0 else 4*q**(len(data)-math.ceil(rate*s['delayMs']*.001))/(1-q)
        require(residual<=1e-6,'insufficiently decayed impulse window')
        require(len(c['complex_response'])==7,'missing complex response')
        require(abs(math.fsum(data)-c['complex_response'][0]['real'])<=1e-8,'raw DC transform mismatch')
        for p in c['complex_response']:
            expected=transfer(rate,s,p['hz']);actual=complex(p['real'],p['imag']);err=abs(expected-actual)
            require(err<=(abs(expected)*1e-4 if abs(expected)>=1e-4 else 1e-5)+residual,'complex response mismatch')
        results.append({'id':c['id'],'max_impulse_error':error,'residual_l1_bound':residual})
    automation=report.get('automation',[]);require(len(automation)==6 and {c['rate'] for c in automation}==set(RATES),'incomplete automation matrix')
    for c in automation:
        source=load_float(checked_file(root,c['source_file'],c['source_sha256']));wet=load_float(checked_file(root,c['wet_file'],c['wet_sha256']))
        require(len(source)==len(wet)==c['frames'],'automation length mismatch')
        expected=reference_automation(source,c['rate'],c['initial_settings'],c['events']);error=max(abs(x-y) for x,y in zip(wet,expected))
        require(error<=2e-5,'automation recurrence mismatch');results.append({'rate':c['rate'],'automation_max_error':error})
    return results
def audit_cpu(root,report):
    qualified=report.get('reference_qualified',False)
    require(not qualified or report.get('kind')=='resonant-comb-cpu-reference','diagnostic cannot qualify reference CPU')
    if qualified:
        require(report['runs']==3 and not report['quick'],'reference requires three full runs')
        environment=report['environment']
        require(environment['pinned'] and environment['hybrid_established'] and environment['on_ac'],'reference lacks pinned AC topology evidence')
        require(environment['power_scheme']!='unavailable' and 'selected_efficiency_class=' in environment['affinity'],'reference lacks affinity/power evidence')
        require(report['provenance']['build_configuration']=='Release','reference requires Release')
        require(bool(report['conditions'].strip()),'missing reference conditions')
        require(len(report['cases'])==240,'incomplete reference case metadata')
        for c in report['cases']:
            require(c['ac_before'] and c['ac_after'] and c['power_before']==c['power_after']==environment['power_scheme'],'reference lost AC/power profile')
    raw=checked_file(root,'timings.csv',report['timings_sha256']);summary=checked_file(root,'summary.csv',report['summary_sha256'])
    quiet=checked_file(root,'quiet_decay.csv',report['quiet_decay_sha256'])
    quiet_cases={int(c['id']):c for c in report['cases'] if c['scenario']=='quiet-decay'};quiet_last={}
    for row in csv.DictReader(quiet.open()):
        case=int(row['case']);require(case in quiet_cases,'unexpected quiet trace case')
        frame=int(row['audio_frame']);require(frame>quiet_last.get(case,0),'unordered quiet trace')
        require(math.isfinite(float(row['elapsed_ns'])) and float(row['elapsed_ns'])>=0,'invalid quiet timing')
        require(math.isfinite(float(row['output_peak'])) and float(row['output_peak'])>=0,'nonfinite quiet decay')
        quiet_last[case]=frame
    require(len(quiet_cases)==10*report['runs'] and set(quiet_last)==set(quiet_cases),'incomplete quiet decay matrix')
    for case,frame in quiet_last.items():require(frame/quiet_cases[case]['rate']>=119,'short quiet decay trace')
    configs={int(c['id']):c for c in report['cases']};settled_counts={key:[0,0] for key in configs}
    grouped={}
    residue_counts={}
    for row in csv.DictReader(raw.open()):
        t=float(row['elapsed_ns']);require(math.isfinite(t) and t>=0,'invalid raw timing');case=int(row['case']);residue=int(row['control_residue']);require(0<=residue<=100,'invalid control residue')
        require(case in configs,'unexpected raw timing case');c=configs[case];iteration=int(row['iteration']);require(residue==iteration%101,'control schedule mismatch')
        target=int(row['bypass_target']);require(target in (0,1),'invalid bypass target')
        if c['scenario']=='bypass':
            ramp=math.ceil(.01*c['rate']/c['frames']);dwell=c['bypass_dwell_callbacks'];require(dwell>=ramp+8,'bypass dwell cannot settle')
            require(target==(iteration//dwell)%2,'bypass schedule mismatch')
            if iteration%dwell>=ramp:settled_counts[case][target]+=1
        grouped.setdefault(case,[]).append(t);residue_counts.setdefault(case,[0]*101)[residue]+=1
    results=[]
    rows=list(csv.DictReader(summary.open()));require(len(rows)==80*report['runs'],'incomplete CPU matrix')
    scenarios={'neutral','maximum-feedback-delay','damping-low','damping-high','automation','bypass','oversized','quiet-decay'}
    require({(int(r['run']),int(r['rate']),int(r['capacity']),r['scenario']) for r in rows}=={(run,rate,cap,s) for run in range(report['runs']) for rate in (48000,96000) for cap in (32,64,127,256,1024) for s in scenarios},'incomplete CPU matrix')
    for row in rows:
        times=sorted(grouped.pop(int(row['case']),[]));require(len(times)==int(row['count']) and times,'timing count mismatch')
        p99=times[math.ceil(.99*len(times))-1];deadline=int(row['frames'])/int(row['rate'])*1e9
        require(abs(p99-float(row['p99_ns']))<.01,'p99 mismatch')
        require(abs(p99/deadline-float(row['p99_deadline_fraction']))<1e-10,'deadline fraction mismatch')
        require(row['processing_ok']=='1','DSP processing failed')
        require(int(row['frames'])==(int(row['capacity'])*2+3 if row['scenario']=='oversized' else int(row['capacity'])),'incorrect incoming deadline')
        coverage=100000<=len(times)<=200000 and (row['scenario']!='automation' or min(residue_counts[int(row['case'])])>=100)
        if row['scenario']=='bypass':
            case=int(row['case']);coverage=coverage and min(settled_counts[case])>=100
            require(settled_counts[case]==[configs[case]['settled_active_observations'],configs[case]['settled_bypass_observations']],'bypass settled coverage mismatch')
        require(int(row['automation_min_count'])==min(residue_counts[int(row['case'])]),'automation coverage mismatch')
        if qualified:
            require(coverage and float(row['window_seconds'])>=10 and float(row['window_seconds'])<=120 and float(row['warmup_seconds'])>=2 and float(row['probe_seconds'])>=2,'reference timing coverage failure')
            require(p99/deadline<.05,'reference budget failure')
        results.append({'case':row['case'],'within_budget':p99/deadline<.05,'p99_deadline_fraction':p99/deadline,'reference_sampling_coverage':coverage})
    require(not grouped,'unexpected raw timing case')
    return results
def main():
    p=argparse.ArgumentParser();p.add_argument('directory',type=Path);p.add_argument('--executable',required=True,type=Path);p.add_argument('--output',type=Path);args=p.parse_args()
    report=json.loads((args.directory/'result.json').read_text());finite_tree(report)
    provenance=report['provenance'];require(digest(args.executable)==provenance['executable_sha256'],'executable hash mismatch')
    root=Path(__file__).resolve().parents[1];names={item['path'] for item in provenance['sources']}
    require({'src/dsp/ResonantComb.h','tools/ResonantCombMeasureMain.cpp','scripts/validate_comb_measurements.py'}<=names,'missing comb source identity')
    for item in provenance['sources']:checked_file(root,item['path'],item['sha256'])
    result={'kind':report['kind'],'reference_qualified':report.get('reference_qualified',False),'listening_approved':False,'cases':audit_cpu(args.directory,report) if report['kind'].startswith('resonant-comb-cpu-') else audit_measurements(args.directory,report)}
    text=json.dumps(result,indent=2)
    if args.output:require(not args.output.exists(),'audit output exists');args.output.write_text(text+'\n')
    print(text)
if __name__=='__main__':
    try:main()
    except (ValueError,KeyError,OSError) as error:print(str(error),file=sys.stderr);sys.exit(1)
