import math

RATE = 2000
MAX_APPROACH_S = 300
DEFAULT_SIGNALS = [f'axis{a}_{field}' for a in range(2) for field in
                   ('target_mm','reference_mm','position_mm','requested_duty','output_duty',
                    'kp','ki','kd','d_filter_hz','slew_enabled','slew_rate_mm_s')]+['do0_command','do0','runner_row','runner_cycles']


def sine(period_s, amplitudes, offsets, phase_deg, pulse_width_ms):
    if len(amplitudes)!=2 or len(offsets)!=2 or not all(math.isfinite(v) for v in [period_s,phase_deg,pulse_width_ms,*amplitudes,*offsets]):
        raise ValueError('Waveform parameters must be finite')
    if not .001 <= period_s <= 300 or min(amplitudes)<0 or pulse_width_ms<0:
        raise ValueError('Period must be 0.001–300 seconds; amplitudes and pulse width must be nonnegative')
    ticks=round(period_s*RATE);pulse=round(pulse_width_ms*RATE/1000)
    if pulse>=ticks or (pulse_width_ms>0 and pulse==0):
        raise ValueError('Pulse must be at least one tick and shorter than the sine period; zero disables it')
    phase=math.radians(phase_deg % 360)
    if abs(offsets[0])>1e-9 or abs(offsets[1]+amplitudes[1]*math.sin(phase))>1e-9:
        raise ValueError('Both sine axes must start at 0 mm; adjust offsets/phase (zero offsets allow 0 or 180 degrees)')
    values=[]
    for tick in range(ticks):
        angle=2*math.pi*tick/ticks
        values.extend((offsets[0]+amplitudes[0]*math.sin(angle),offsets[1]+amplitudes[1]*math.sin(angle+phase),1.0 if tick<pulse else 0.0))
    values[:2]=[0.0,0.0]  # Remove roundoff at an accepted zero crossing.
    return values, ticks/RATE, pulse/RATE*1000


def approach_zero(positions, speed_mm_s):
    if len(positions)!=2 or not all(math.isfinite(v) for v in [*positions,speed_mm_s]) or speed_mm_s<=0:
        raise ValueError('Approach needs two finite positions and a positive finite speed')
    duration=max(abs(v) for v in positions)/speed_mm_s
    if duration>MAX_APPROACH_S:
        raise ValueError('Approach exceeds 300 seconds; increase approach speed or move closer to zero')
    ticks=max(1,math.ceil(duration*RATE))
    values=[]
    for tick in range(ticks+1):
        remaining=(ticks-tick)/ticks
        values.extend((positions[0]*remaining,positions[1]*remaining,0.0))
    return values,ticks/RATE
