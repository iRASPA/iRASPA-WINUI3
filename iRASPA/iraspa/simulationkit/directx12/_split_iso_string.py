from pathlib import Path

t = Path(r'C:\Users\ddubb\source\repos\iRASPA-QT\iraspa\simulationkit\directx12\skcomputeisosurface_kernels.hlsl.txt').read_text(encoding='utf-8')
chunk = 15000
parts = [t[i:i + chunk] for i in range(0, len(t), chunk)]
lines = []
for j, part in enumerate(parts):
    delim = f'iso{j}'
    while f'){delim}' in part:
        delim += 'x'
    sep = ' +' if j < len(parts) - 1 else ''
    lines.append(f'std::string(R"{delim}({part}){delim}"){sep}')
Path(r'C:\Users\ddubb\source\repos\iRASPA-QT\iraspa\simulationkit\directx12\skcomputeisosurface_kernel_string.inc').write_text('\n'.join(lines) + '\n', encoding='utf-8')
print('parts', len(parts))
