"""Non-RT Linux usage sampling; cached so multiple browser tabs share one sample."""
import shutil
import time
from pathlib import Path


class SystemStatus:
    def __init__(self, root, proc=Path('/proc'), cpu_sys=Path('/sys/devices/system/cpu')):
        self.root, self.proc, self.cpu_sys = root, proc, cpu_sys
        self.previous = {}
        self.updated = -1.0
        self.cached = {}

    def snapshot(self):
        now = time.monotonic()
        if now - self.updated < 1:
            return self.cached
        usage = {}
        try:
            for line in (self.proc / 'stat').read_text().splitlines():
                fields = line.split()
                if not fields or not fields[0].startswith('cpu'):
                    continue
                values = list(map(int, fields[1:]))
                total, idle = sum(values[:8]), values[3] + values[4]
                previous = self.previous.get(fields[0])
                delta = total - previous[0] if previous else 0
                usage[fields[0]] = round(max(0, min(100, 100 * (1 - (idle - previous[1]) / delta))), 1) if delta > 0 else None
                self.previous[fields[0]] = total, idle
            memory = {line.split(':')[0]: int(line.split()[1]) * 1024
                      for line in (self.proc / 'meminfo').read_text().splitlines() if ':' in line}
            total = memory['MemTotal']
            used = total - memory.get('MemAvailable', memory.get('MemFree', 0))
            ram = {'used': used, 'total': total, 'percent': round(100 * used / total, 1)}
        except (OSError, ValueError, KeyError, IndexError):
            ram = None
        disk = shutil.disk_usage(self.root)
        gpu = self.optional_number('/sys/class/drm/card0/device/gpu_busy_percent')
        temperature = self.optional_number('/sys/class/thermal/thermal_zone0/temp')
        self.cached = {
            'cpu_percent': usage.get('cpu'),
            'cpu_cores': [value for key, value in usage.items() if key != 'cpu'],
            'isolated_cpus': self.read_cpu_list(self.cpu_sys / 'isolated'),
            'memory': ram,
            'disk': {'used': disk.used, 'total': disk.total, 'percent': round(100 * disk.used / disk.total, 1)},
            'gpu_percent': gpu,
            'temperature_c': temperature / 1000 if temperature is not None else None,
        }
        self.updated = now
        return self.cached

    @staticmethod
    def read_cpu_list(path):
        try:
            cores = []
            for group in Path(path).read_text().strip().split(','):
                if group:
                    span = list(map(int, group.split('-')))
                    cores.extend(range(span[0], span[-1] + 1))
            return cores
        except (OSError, ValueError):
            return None

    @staticmethod
    def optional_number(path):
        try:
            return float(Path(path).read_text().strip())
        except (OSError, ValueError):
            return None
