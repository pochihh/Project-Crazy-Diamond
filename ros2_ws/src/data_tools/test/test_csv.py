import pytest
from data_tools.csv_program import parse_csv,library_path

def test_headers_and_values():
    channels,values,warnings=parse_csv('# note\naxis0_reference_mm,axis1_reference_mm,do0,unused\n1,2,1,x\n3,4,0,x\n')
    assert channels==[0,1,12] and values==[1,2,1,3,4,0] and len(warnings)==1

@pytest.mark.parametrize('csv', ['axis0_duty\nnan\n','do0\n2\n','axis0_duty,axis0_reference_mm\n0,0\n','axis0_duty,axis0_duty\n0,0\n','do0,do1\n1\n','unknown\n1\n'])
def test_invalid(csv):
    with pytest.raises(ValueError):parse_csv(csv)

def test_mixed_position_units_and_preview():
    from data_tools.csv_program import preview_bins
    channels,values,_=parse_csv('axis0_reference_mm,axis2_reference_deg\n1,-19.5\n')
    assert channels==[0,20] and values==[1,-19.5]
    assert preview_bins(channels,values)['channels']==['axis0_reference_mm','axis2_reference_deg']
    with pytest.raises(ValueError):parse_csv('axis2_reference_deg,axis2_reference_mm\n1,1\n')
    with pytest.raises(ValueError):parse_csv('axis2_reference_deg,axis2_duty\n1,0\n')


def test_paths(tmp_path):
    assert library_path(tmp_path,'experiment.csv').parent==tmp_path
    for name in ['../a.csv','/tmp/a.csv','a.txt']:
        with pytest.raises(ValueError):library_path(tmp_path,name)


def test_preview_retains_one_tick_pulse_and_final_short_bin():
    from data_tools.csv_program import preview_bins
    preview = preview_bins([12, 0], [0, 3, 1, -8, 0, 2, 0, 1, 1, 4], max_bins=2)
    assert preview == dict(rows=5, rows_per_bin=3, channels=['do0', 'axis0_reference_mm'],
                           minimum=[0, -8, 0, 1], maximum=[1, 3, 1, 4])
