#!/usr/bin/env python3
"""Reproducible CPU1/CPU2 builds using the installed TI CCS compiler/DriverLib."""
import argparse
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('configuration', choices=['RAM', 'FLASH'])
args = parser.parse_args()
ccs = Path(os.environ.get('TI_CCS_ROOT', '/Applications/ti/ccs2020/ccs'))
cgt = Path(os.environ.get('TI_CGT_ROOT', str(ccs / 'tools/compiler/ti-cgt-c2000_22.6.2.LTS')))
ware = Path(os.environ.get('C2000WARE_ROOT', str(Path.home() / 'ti/C2000Ware_6_00_01_00')))
driver = ware / 'driverlib/f2837xd/driverlib'
compiler = cgt / 'bin/cl2000'
for path in [compiler, driver / 'ccs/Debug/driverlib.lib']:
    if not path.is_file():
        parser.error(f'Missing TI dependency: {path}; set TI_CCS_ROOT, TI_CGT_ROOT or C2000WARE_ROOT')
for cpu in [1, 2]:
    source = root / f'dsp/f28379d/cpu{cpu}'
    build = source / args.configuration
    build.mkdir(exist_ok=True)
    flags = ['-v28', '-ml', '-mt', '--float_support=fpu32', '--tmu_support=tmu0', '--vcu_support=vcu0',
             '-O2', '-g', '--abi=eabi', '--define=DEBUG', f'--define=CPU{cpu}', '--diag_warning=225',
             '--diag_suppress=10063', '--display_error_number']
    if args.configuration == 'FLASH':
        flags.append('--define=_FLASH')
    for include in [source, source.parent / 'device', source.parent / 'shared', driver, cgt / 'include']:
        flags.append(f'--include_path={include}')
    names = ['main.c', 'spi_dma.c', 'motor.c', 'encoders.c', 'io.c'] if cpu == 1 else ['main.c']
    sources = [source / n for n in names] + [source.parent / 'device' / n for n in ['device.c', 'F2837xD_CodeStartBranch.asm']]
    objects = []
    for path in sources:
        obj = build / (path.stem + '.obj')
        subprocess.run([str(compiler), *flags, '-c', str(path), f'--output_file={obj}'], cwd=build, check=True)
        objects.append(str(obj))
    output = build / f'f28379d_cpu{cpu}.out'
    subprocess.run([str(compiler), *flags, '-z', '--rom_model', '--entry_point=code_start',
                    '--stack_size=0x3f8', '--heap_size=0x200', '--warn_sections',
                    f'-i{cgt / "lib"}', f'-i{cgt / "include"}', f'-m{output.with_suffix(".map")}',
                    '-o', str(output), *objects, str(source / f'2837xD_{args.configuration}_lnk_cpu{cpu}.cmd'),
                    str(driver / 'ccs/Debug/driverlib.lib'), '-llibc.a'], cwd=build, check=True)
    print(output, flush=True)
