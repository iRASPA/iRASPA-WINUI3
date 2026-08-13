from pathlib import Path
import re

src = Path(r'C:\Users\ddubb\source\repos\iRASPA-QT\iraspa\simulationkit\opencl\skcomputeisosurface.cpp').read_text(encoding='utf-8', errors='replace')
numTris = re.search(r'numberOfTriangles\[256\]\s*=\s*\{([^}]+)\}', src).group(1).strip()
tri = re.search(r'triTable\[4096\]\s*=\s*\{([^}]+)\}', src, re.S).group(1).strip()
off = re.search(r'offsets3\[72\]\s*=\s*\{([^}]+)\}', src, re.S).group(1).strip()
off_clean = re.sub(r'//[^\n]*', '', off)
off_clean = ','.join(p.strip() for p in off_clean.split(',') if p.strip())

hlsl = r'''// Histo-pyramid marching cubes — structured-buffer DX12 port of OpenCL kernels
static const int3 cubeOffsets[8] = {
  int3(0,0,0), int3(1,0,0), int3(0,0,1), int3(1,0,1),
  int3(0,1,0), int3(1,1,0), int3(0,1,1), int3(1,1,1)
};

static const int offsets3[72] = {
''' + off_clean + r'''
};

static const uint numberOfTriangles[256] = {
''' + numTris + r'''
};

static const int triTable[4096] = {
''' + tri + r'''
};

cbuffer IsoConstants : register(b0)
{
  int4 dimensions;   // xyz logical dims
  float isolevel;
  int sumTriangles;
  int gridSize;      // encompassing power-of-two size
  int levelSize;     // for construct: write side size
};

StructuredBuffer<float> rawData : register(t0);
RWStructuredBuffer<uint4> hpBase : register(u0);
RWStructuredBuffer<uint> hpLevels : register(u1); // concatenated pyramid levels (except base)
RWStructuredBuffer<float4> VBOBuffer : register(u2);

// Per-pass buffers rebound via descriptors:
StructuredBuffer<uint4> constructInBase : register(t1);
StructuredBuffer<uint> constructInLevel : register(t2);
RWStructuredBuffer<uint> constructOut : register(u3);

StructuredBuffer<uint4> travHp0 : register(t10);
StructuredBuffer<uint> travHp1 : register(t11);
StructuredBuffer<uint> travHp2 : register(t12);
StructuredBuffer<uint> travHp3 : register(t13);
StructuredBuffer<uint> travHp4 : register(t14);
StructuredBuffer<uint> travHp5 : register(t15);
StructuredBuffer<uint> travHp6 : register(t16);
StructuredBuffer<uint> travHp7 : register(t17);
StructuredBuffer<uint> travHp8 : register(t18);

uint idx3(int x, int y, int z, int size)
{
  return (uint)(x + y * size + z * size * size);
}

int3 wrapDim(int3 p)
{
  int3 d = dimensions.xyz;
  p.x = ((p.x % d.x) + d.x) % d.x;
  p.y = ((p.y % d.y) + d.y) % d.y;
  p.z = ((p.z % d.z) + d.z) % d.z;
  return p;
}

float sampleRaw(int3 p)
{
  p = wrapDim(p);
  // voxels stored at encompassing gridSize
  return rawData[idx3(p.x, p.y, p.z, gridSize)];
}

uint loadHpU(StructuredBuffer<uint> buf, int3 p, int size)
{
  return buf[idx3(p.x, p.y, p.z, size)];
}

uint4 loadHpBase(StructuredBuffer<uint4> buf, int3 p, int size)
{
  return buf[idx3(p.x, p.y, p.z, size)];
}

[numthreads(8,8,8)]
void classifyCubes(uint3 id : SV_DispatchThreadID)
{
  int3 pos = int3(id);
  if (any(pos >= (int3)gridSize))
    return;
  if (any(pos >= dimensions.xyz))
  {
    hpBase[idx3(pos.x, pos.y, pos.z, gridSize)] = uint4(0,0,0,0);
    return;
  }
  float first = sampleRaw(pos);
  uint cubeindex =
      ((first > isolevel) ? 1u : 0u) |
      ((sampleRaw(pos + cubeOffsets[1]) > isolevel) ? 2u : 0u) |
      ((sampleRaw(pos + cubeOffsets[3]) > isolevel) ? 4u : 0u) |
      ((sampleRaw(pos + cubeOffsets[2]) > isolevel) ? 8u : 0u) |
      ((sampleRaw(pos + cubeOffsets[4]) > isolevel) ? 16u : 0u) |
      ((sampleRaw(pos + cubeOffsets[5]) > isolevel) ? 32u : 0u) |
      ((sampleRaw(pos + cubeOffsets[7]) > isolevel) ? 64u : 0u) |
      ((sampleRaw(pos + cubeOffsets[6]) > isolevel) ? 128u : 0u);
  hpBase[idx3(pos.x, pos.y, pos.z, gridSize)] = uint4(numberOfTriangles[cubeindex], cubeindex, 0, 0);
}

[numthreads(8,8,8)]
void constructHPLevelFromBase(uint3 id : SV_DispatchThreadID)
{
  int3 writePos = int3(id);
  if (any(writePos >= (int3)levelSize))
    return;
  int3 readPos = writePos * 2;
  int readSize = levelSize * 2;
  uint sumv = 0;
  [unroll] for (int i = 0; i < 8; ++i)
  {
    int3 rp = readPos + cubeOffsets[i];
    sumv += loadHpBase(constructInBase, rp, readSize).x;
  }
  constructOut[idx3(writePos.x, writePos.y, writePos.z, levelSize)] = sumv;
}

[numthreads(8,8,8)]
void constructHPLevel(uint3 id : SV_DispatchThreadID)
{
  int3 writePos = int3(id);
  if (any(writePos >= (int3)levelSize))
    return;
  int3 readPos = writePos * 2;
  int readSize = levelSize * 2;
  uint sumv = 0;
  [unroll] for (int i = 0; i < 8; ++i)
  {
    int3 rp = readPos + cubeOffsets[i];
    sumv += loadHpU(constructInLevel, rp, readSize);
  }
  constructOut[idx3(writePos.x, writePos.y, writePos.z, levelSize)] = sumv;
}

int4 scanHPLevelBase(int target, StructuredBuffer<uint4> hp, int size, int4 current)
{
  int neighbors[8];
  [unroll] for (int i = 0; i < 8; ++i)
    neighbors[i] = (int)loadHpBase(hp, current.xyz + cubeOffsets[i], size).x;
  int acc = current.w + neighbors[0];
  int cmp[8];
  cmp[0] = acc <= target;
  [unroll] for (int i = 1; i < 7; ++i) { acc += neighbors[i]; cmp[i] = acc <= target; }
  cmp[7] = 0;
  int isum = cmp[0]+cmp[1]+cmp[2]+cmp[3]+cmp[4]+cmp[5]+cmp[6]+cmp[7];
  current.xyz += cubeOffsets[isum];
  current.xyz *= 2;
  current.w = current.w
    + cmp[0]*neighbors[0] + cmp[1]*neighbors[1] + cmp[2]*neighbors[2] + cmp[3]*neighbors[3]
    + cmp[4]*neighbors[4] + cmp[5]*neighbors[5] + cmp[6]*neighbors[6] + cmp[7]*neighbors[7];
  return current;
}

int4 scanHPLevelU(int target, StructuredBuffer<uint> hp, int size, int4 current)
{
  int neighbors[8];
  [unroll] for (int i = 0; i < 8; ++i)
    neighbors[i] = (int)loadHpU(hp, current.xyz + cubeOffsets[i], size);
  int acc = current.w + neighbors[0];
  int cmp[8];
  cmp[0] = acc <= target;
  [unroll] for (int i = 1; i < 7; ++i) { acc += neighbors[i]; cmp[i] = acc <= target; }
  cmp[7] = 0;
  int isum = cmp[0]+cmp[1]+cmp[2]+cmp[3]+cmp[4]+cmp[5]+cmp[6]+cmp[7];
  current.xyz += cubeOffsets[isum];
  current.xyz *= 2;
  current.w = current.w
    + cmp[0]*neighbors[0] + cmp[1]*neighbors[1] + cmp[2]*neighbors[2] + cmp[3]*neighbors[3]
    + cmp[4]*neighbors[4] + cmp[5]*neighbors[5] + cmp[6]*neighbors[6] + cmp[7]*neighbors[7];
  return current;
}
'''

def make_traverse(name, n_levels):
    # sizes: level 0 = gridSize, level k = gridSize / 2^k
    scans = []
    for level in range(n_levels - 1, 0, -1):
        scans.append(f'  cubePosition = scanHPLevelU(target, travHp{level}, max(gridSize >> {level}, 1), cubePosition);')
    scans.append('  cubePosition = scanHPLevelBase(target, travHp0, gridSize, cubePosition);')
    return f'''
[numthreads(64,1,1)]
void {name}(uint3 dtid : SV_DispatchThreadID)
{{
  int target = (int)dtid.x;
  if (target >= sumTriangles)
    target = 0;

  int4 cubePosition = int4(0,0,0,0);
{chr(10).join(scans)}
  cubePosition.xyz /= 2;

  int vertexNr = 0;
  uint4 cubeData = loadHpBase(travHp0, cubePosition.xyz, gridSize);

  for (int i = (target - cubePosition.w) * 3; i < (target - cubePosition.w + 1) * 3; ++i)
  {{
    int edge = triTable[cubeData.y * 16 + i];
    int3 point0 = int3(cubePosition.x + offsets3[edge * 6],
                       cubePosition.y + offsets3[edge * 6 + 1],
                       cubePosition.z + offsets3[edge * 6 + 2]);
    int3 point1 = int3(cubePosition.x + offsets3[edge * 6 + 3],
                       cubePosition.y + offsets3[edge * 6 + 4],
                       cubePosition.z + offsets3[edge * 6 + 5]);

    float4 forwardDifference0 = float4(
      -sampleRaw(point0 + int3(1,0,0)) + sampleRaw(point0 + int3(-1,0,0)),
      -sampleRaw(point0 + int3(0,1,0)) + sampleRaw(point0 + int3(0,-1,0)),
      -sampleRaw(point0 + int3(0,0,1)) + sampleRaw(point0 + int3(0,0,-1)),
      0.0f);
    float4 forwardDifference1 = float4(
      -sampleRaw(point1 + int3(1,0,0)) + sampleRaw(point1 + int3(-1,0,0)),
      -sampleRaw(point1 + int3(0,1,0)) + sampleRaw(point1 + int3(0,-1,0)),
      -sampleRaw(point1 + int3(0,0,1)) + sampleRaw(point1 + int3(0,0,-1)),
      0.0f);

    float value0 = sampleRaw(point0);
    float value1 = sampleRaw(point1);
    float diff = (isolevel - value0) / (value1 - value0);

    float4 vertex = float4(point0, 1.0f) + (float4(point1, 0.0f) - float4(point0, 0.0f)) * diff;
    float4 scaledVertex = float4(vertex.x / (float)dimensions.x,
                                 vertex.y / (float)dimensions.y,
                                 vertex.z / (float)dimensions.z, 1.0f);
    float4 normal = forwardDifference0 + (forwardDifference1 - forwardDifference0) * diff;

    VBOBuffer[target * 9 + vertexNr * 3] = scaledVertex;
    VBOBuffer[target * 9 + vertexNr * 3 + 1] = normal;
    VBOBuffer[target * 9 + vertexNr * 3 + 2] = float4(0,0,0,0);
    vertexNr++;
  }}
}}
'''

for n, name in [(4,'traverseHP16'),(5,'traverseHP32'),(6,'traverseHP64'),(7,'traverseHP128'),(8,'traverseHP256'),(9,'traverseHP512')]:
    hlsl += make_traverse(name, n)

out = Path(r'C:\Users\ddubb\source\repos\iRASPA-QT\iraspa\simulationkit\directx12\skcomputeisosurface_kernels.hlsl.txt')
out.write_text(hlsl, encoding='utf-8')
frag = Path(r'C:\Users\ddubb\source\repos\iRASPA-QT\iraspa\simulationkit\directx12\skcomputeisosurface_kernel_string.inc')
frag.write_text('R"iso(\n' + hlsl + '\n)iso"', encoding='utf-8')
print('done', out.stat().st_size)
