import pytest
from phantom_robot.waveform import sine, approach_zero, RATE

def test_sine_pulse():
    values,period,pulse=sine(2,[1,2],[0,0],180,5)
    assert period==2 and pulse==5 and len(values)==12000
    assert values[0]==0 and values[1]==0
    assert values[2:30:3]==[1]*10 and sum(values[2::3])==10
    assert values[3000]==pytest.approx(1)
    assert values[3001]==pytest.approx(-2)


@pytest.mark.parametrize('offsets,phase', [([1,0],0),([0,1],0),([0,0],90)])
def test_reject_nonzero_sine_start(offsets,phase):
    with pytest.raises(ValueError,match='start at 0'):
        sine(2,[1,2],offsets,phase,5)


def test_offset_phase_combination_must_still_start_at_zero():
    values,_,_=sine(2,[1,2],[0,-2],90,5)
    assert values[:2]==[0,0]


def test_approach_starts_at_measurement_and_reaches_zero_at_bounded_speed():
    values,duration=approach_zero([12.3,-4.5],5)
    rows=list(zip(values[0::3],values[1::3],values[2::3]))
    assert rows[0]==(12.3,-4.5,0) and rows[-1]==(0,0,0)
    assert duration==pytest.approx(2.46)
    assert all(abs(b[a]-r[a])<=5/RATE+1e-12 for r,b in zip(rows,rows[1:]) for a in (0,1))
    assert all(r[2]==0 for r in rows)
    assert approach_zero([0,0],5)[0]==[0,0,0,0,0,0]


@pytest.mark.parametrize('positions,speed', [([1,2],0),([1,2],float('nan')),([float('inf'),0],5),([1e9,0],5),([0],5)])
def test_bad_approach(positions,speed):
    with pytest.raises(ValueError):approach_zero(positions,speed)

@pytest.mark.parametrize('period,pulse', [(0,5),(1,1000),(1,.01),(float('nan'),5)])
def test_bad_timing(period,pulse):
    with pytest.raises(ValueError):sine(period,[1,1],[0,0],0,pulse)
