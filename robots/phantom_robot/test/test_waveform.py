import pytest
from phantom_robot.waveform import sine

def test_sine_pulse():
    values,period,pulse=sine(2,[1,2],[0,1],90,5)
    assert period==2 and pulse==5 and len(values)==12000
    assert values[0]==0 and values[1]==3
    assert values[2:30:3]==[1]*10 and sum(values[2::3])==10
    assert values[3000]==pytest.approx(1)

@pytest.mark.parametrize('period,pulse', [(0,5),(1,1000),(1,.01),(float('nan'),5)])
def test_bad_timing(period,pulse):
    with pytest.raises(ValueError):sine(period,[1,1],[0,0],0,pulse)
