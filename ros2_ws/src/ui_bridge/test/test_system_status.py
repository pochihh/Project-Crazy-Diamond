from ui_bridge.system_status import SystemStatus


def test_linux_usage_delta_capacity_and_shared_cache(tmp_path, monkeypatch):
    monkeypatch.setattr('ui_bridge.system_status.time.monotonic', lambda: 10)
    (tmp_path / 'stat').write_text('cpu 100 0 20 80 0 0 0 0\ncpu0 100 0 20 80 0 0 0 0\n')
    (tmp_path / 'meminfo').write_text('MemTotal: 1000 kB\nMemAvailable: 250 kB\n')
    status = SystemStatus(tmp_path, tmp_path)
    monkeypatch.setattr(status, 'optional_number', lambda _: None)
    first = status.snapshot()
    assert first['cpu_percent'] is None and first['gpu_percent'] is None
    assert first['memory'] == {'used': 750 * 1024, 'total': 1000 * 1024, 'percent': 75.0}
    assert first['disk']['total'] >= first['disk']['used'] > 0
    (tmp_path / 'stat').write_text('cpu 150 0 20 130 0 0 0 0\ncpu0 150 0 20 130 0 0 0 0\n')
    assert status.snapshot() is first
    monkeypatch.setattr('ui_bridge.system_status.time.monotonic', lambda: 12)
    second = status.snapshot()
    assert second['cpu_percent'] == 50 and second['cpu_cores'] == [50]


def test_missing_proc_is_reported_as_unavailable(tmp_path):
    result = SystemStatus(tmp_path, tmp_path).snapshot()
    assert result['cpu_percent'] is None and result['memory'] is None


def test_cpu_isolation_handles_single_cores_ranges_and_unavailable(tmp_path):
    path = tmp_path / 'isolated'
    assert SystemStatus.read_cpu_list(path) is None
    for text, expected in [('3', [3]), ('0-2,5', [0, 1, 2, 5]), ('', []), ('unknown', None)]:
        path.write_text(text)
        assert SystemStatus.read_cpu_list(path) == expected
