import math

RATE = 2000
DEFAULT_SIGNALS = [f'axis{a}_{field}' for a in range(2) for field in
                   ('target_mm','reference_mm','position_mm','requested_duty','output_duty',
                    'kp','ki','kd','slew_enabled','slew_rate_mm_s')]+['do0_command','do0','runner_row','runner_cycles']


def sine(period_s, amplitudes, offsets, phase_deg, pulse_width_ms):
    if len(amplitudes)!=2 or len(offsets)!=2 or not all(math.isfinite(v) for v in [period_s,phase_deg,pulse_width_ms,*amplitudes,*offsets]):
        raise ValueError('Waveform parameters must be finite')
    if not .001 <= period_s <= 300 or min(amplitudes)<0 or pulse_width_ms<0:
        raise ValueError('Period must be 0.001–300 seconds; amplitudes and pulse width must be nonnegative')
    ticks=round(period_s*RATE);pulse=round(pulse_width_ms*RATE/1000)
    if pulse>=ticks or (pulse_width_ms>0 and pulse==0):
        raise ValueError('Pulse must be at least one tick and shorter than the sine period; zero disables it')
    phase=math.radians(phase_deg);values=[]
    for tick in range(ticks):
        angle=2*math.pi*tick/ticks
        values.extend((offsets[0]+amplitudes[0]*math.sin(angle),offsets[1]+amplitudes[1]*math.sin(angle+phase),1.0 if tick<pulse else 0.0))
    return values, ticks/RATE, pulse/RATE*1000
