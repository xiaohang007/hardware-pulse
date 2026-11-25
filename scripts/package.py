"""Package a built x64 Lite executable and its runtime resources."""
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile

root = Path(__file__).resolve().parents[1]
executable = root / 'Bin/x64/Release (lite)/HardwarePulse.exe'
output = root / 'Bin/HardwarePulse-x64.zip'
skins = root / 'TrafficMonitor/skins'

with ZipFile(output, 'w', compression=ZIP_DEFLATED) as archive:
    archive.write(executable, 'HardwarePulse/HardwarePulse.exe')
    for name in ('LICENSE', 'LICENSE_CN'):
        archive.write(root / name, f'HardwarePulse/{name}')
    archive.writestr('HardwarePulse/global_cfg.ini', '[config]\r\nportable_mode=1\r\n')
    for asset in sorted(skins.rglob('*')):
        if asset.is_file():
            archive.write(asset, f'HardwarePulse/skins/{asset.relative_to(skins).as_posix()}')
with ZipFile(output) as archive:
    if archive.testzip() is not None:
        raise RuntimeError('Portable archive failed its integrity check')
print(output)
