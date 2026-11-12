import pytest
from data_tools.csv_program import parse_csv,library_path

def test_headers_and_values():
    channels,values,warnings=parse_csv('# note\naxis0_reference_mm,axis1_reference_mm,do0,unused\n1,2,1,x\n3,4,0,x\n')
    assert channels==[0,1,12] and values==[1,2,1,3,4,0] and len(warnings)==1

@pytest.mark.parametrize('csv', ['axis0_duty\nnan\n','do0\n2\n','axis0_duty,axis0_reference_mm\n0,0\n','axis0_duty,axis0_duty\n0,0\n','do0,do1\n1\n','unknown\n1\n'])
def test_invalid(csv):
    with pytest.raises(ValueError):parse_csv(csv)

def test_paths(tmp_path):
    assert library_path(tmp_path,'experiment.csv').parent==tmp_path
    for name in ['../a.csv','/tmp/a.csv','a.txt']:
        with pytest.raises(ValueError):library_path(tmp_path,name)
