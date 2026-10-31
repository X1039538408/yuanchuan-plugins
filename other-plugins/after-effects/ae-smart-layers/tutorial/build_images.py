"""Rebuild release tutorial diagrams and synthetic-only actual-worker evidence.
Run from the development workspace with Pillow and NumPy installed.
No user-provided source image is read. All writes stay inside release-docs.
"""
from pathlib import Path
import colorsys
import hashlib
import json
import subprocess
import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
DOCS = HERE.parent
ROOT = DOCS.parents[2]
JSX = DOCS / 'source' / 'AE 智能分层.jsx'
if not JSX.exists():
    JSX = ROOT / 'AE 智能分层.jsx'
OUT = HERE / 'images'
WORK = DOCS / '.build-evidence'
OUT.mkdir(parents=True, exist_ok=True)
WORK.mkdir(parents=True, exist_ok=True)
FONT = r'C:\Windows\Fonts\msyh.ttc'
BOLD = r'C:\Windows\Fonts\msyhbd.ttc'
BG = '#eef3f9'
INK = '#15243c'
MUTED = '#536780'
BLUE = '#2258ca'

def font(size=28, bold=False):
    return ImageFont.truetype(BOLD if bold else FONT, size)

def text(draw, xy, value, size=28, fill=INK, bold=False):
    draw.text(xy, value, font=font(size, bold), fill=fill, spacing=15)

def base(title, subtitle, height):
    im = Image.new('RGB', (1600, height), BG)
    d = ImageDraw.Draw(im)
    d.rectangle((0, 0, 1600, 166), fill=INK)
    text(d, (64, 29), title, 45, 'white', True)
    text(d, (66, 103), subtitle, 25, '#c8d9f3')
    return im, d

def card(d, box, number, title, lines):
    x, y, x2, y2 = box
    d.rounded_rectangle(box, radius=22, fill='white', outline='#d8e2ee', width=2)
    d.ellipse((x+25, y+24, x+87, y+86), fill=BLUE)
    text(d, (x+43, y+31), str(number), 32, 'white', True)
    text(d, (x+108, y+32), title, 30, INK, True)
    for i, line in enumerate(lines):
        text(d, (x+30, y+111+i*46), line, 27, MUTED)

def footer(d, height, extra='流程示意，非 AE 真机截图'):
    text(d, (66, height-62), extra, 24, MUTED)

im, d = base('01  安装：定位 AE，再安装脚本', 'Windows · AE 智能分层 1.0.0 / portable14', 1080)
card(d, (65, 205, 770, 495), 1, '启动安装器', ['运行 AESmartLayers-Setup-1.0.0.exe', '根据安装界面选择目标 AE 版本。', '系统目录写入可能需要管理员权限。'])
card(d, (830, 205, 1535, 495), 2, '确认 AE 位置', ['选择 AE 根目录或 Support Files。', '应能定位 Support Files / AfterFX.exe。', '不要选工程目录或插件 Plug-ins 目录。'])
card(d, (65, 540, 1535, 865), 3, '确认安装目标', ['AE 根目录 / Support Files / Scripts / AE 智能分层.jsx', '脚本自包含；无需把维护源码复制到 AE。', '安装器不自动更改 AE 安全偏好，不强制重启，不保存工程。'])
text(d, (95, 900), '菜单未出现？直接使用「文件 → 脚本 → 运行脚本文件…」选择已安装 JSX。', 28, BLUE, True)
footer(d, im.height)
im.save(OUT / 'install-flow.png')

im, d = base('02  使用：先选源，再运行，自动开始', '文件 → 脚本 → 运行脚本文件… → AE 智能分层.jsx', 1260)
steps = [
('保存并准备', ['先自行保存工程，打开目标合成。', '图片 / 预合成：停在要分析的帧。', '动画结果不会跨帧跟踪或自动冻帧。']),
('多选 1–20 个源', ['可混选图片、预合成、原生文字。', '仅静态 2D、无父级、非分离 Position。', '原生文字仅静态水平 LTR 点文字。']),
('从 AE 文件菜单运行', ['文件 → 脚本 → 运行脚本文件…', '选择 AE 智能分层.jsx。', '无需「开始」按钮或参数面板。']),
('查看真实阶段进度', ['准备源 → 图像分析 → 生成输出。', '按完成源数 / 输出数显示。', '取消或关窗会请求整批清理。']),
('成功后检查结果', ['源层隐藏保留；输出全部选中。', '原生文字：可编辑单字文字层。', '图片 / 预合成：区域蒙版图层。']),
('叠加复核再保存', ['临时开启原层，对照位置与边缘。', '图片「文字」标签不等于可编辑字。', '每批最多 120 输出；自行保存。']),
]
for i, (title, lines) in enumerate(steps):
    x = 65 + (i % 2)*765
    y = 204 + (i // 2)*316
    card(d, (x, y, x+705, y+282), i+1, title, lines)
footer(d, im.height)
im.save(OUT / 'usage-flow.png')

im, d = base('03  卸载：核对文件，只移除本插件', '先结束分层任务，再运行独立卸载器', 1040)
card(d, (65, 205, 770, 492), 1, '运行卸载器', ['AESmartLayers-Uninstall-1.0.0.exe', '按界面选择当时安装的 AE 目录。', '核对目标后再确认卸载。'])
card(d, (830, 205, 1535, 492), 2, '按哈希匹配保护文件', ['仅删除与本插件清单哈希相符文件。', '手动修改过的插件文件会保留。', '不会清空 Scripts 或删除其他插件。'])
card(d, (65, 538, 1535, 860), 3, '核对卸载结果', ['检查 Support Files / Scripts / AE 智能分层.jsx 及卸载结果提示。', '如果提示文件已修改并保留：先备份，再自行决定是否手动删除该文件。', '卸载不会自动删除工程里已经生成的层、素材或用户工程。'])
footer(d, im.height)
im.save(OUT / 'uninstall-flow.png')

# Same self-drawn geometric fixture recipe as the portable14 regression.
# This recreates ellipses and the word THE; it never opens SECOND / THE THIRD.
def fixture(with_text=False):
    size = (420, 250)
    im = Image.new('RGBA', size)
    colors = [(250,70,85), (40,170,245), (245,185,25), (110,220,95), (185,75,220), (255,125,25)]
    points = [(65,65), (210,65), (355,65), (65,185), (210,185), (355,185)]
    for i, angle in enumerate([-90,-35,25,80,145,205]):
        x, y = points[i]
        mask = Image.new('L', size)
        ImageDraw.Draw(mask).ellipse((x-12,y-34,x+12,y+34), fill=255)
        mask = mask.rotate(angle, resample=Image.Resampling.BICUBIC, center=(x,y))
        arr = np.asarray(im).copy()
        active = np.asarray(mask)>0
        arr[active,:3] = colors[i]
        arr[active,3] = 255
        im = Image.fromarray(arr)
    if with_text:
        ImageDraw.Draw(im).text((155,110), 'THE', font=ImageFont.truetype(r'C:\Windows\Fonts\arial.ttf',23), fill=(245,245,245,255))
    return im

source = JSX.read_text(encoding='utf-8-sig')
block = source.split('var WORKER = [',1)[1].split('].join("\\n");',1)[0]
worker = '\n'.join(json.loads(line.strip().rstrip(',')) for line in block.splitlines() if line.strip())
code = worker.split("$code = @'\n",1)[1].split("\n'@",1)[0]
jobs = []
for name, mixed in [('petals',False), ('petals-mixed',True)]:
    job = WORK / name
    job.mkdir(exist_ok=True)
    fixture(mixed).save(OUT / (name+'-input.png'))
    (job/'source.txt').write_text(str(OUT/(name+'-input.png')), encoding='utf-8-sig')
    jobs.append(job)
harness = WORK/'run-synthetic.ps1'
harness.write_text("$ErrorActionPreference='Stop'\nAdd-Type -AssemblyName System.Drawing\n$code=@'\n"+code+"\n'@\nAdd-Type -TypeDefinition $code -ReferencedAssemblies System.Drawing\n"+'\n'.join("[SplitWorker]::Run('"+str(job).replace("'","''")+"')" for job in jobs), encoding='utf-8-sig')
subprocess.run([r'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe','-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',str(harness)], check=True, timeout=120)

def checker(size):
    image = Image.new('RGB', size, '#263347')
    dr = ImageDraw.Draw(image)
    for y in range(0,size[1],16):
        for x in range(0,size[0],16):
            if (x//16+y//16)%2:
                dr.rectangle((x,y,x+15,y+15), fill='#2f3e52')
    return image

results = []
rendered = []
for job in jobs:
    lines = (job/'result.txt').read_text(encoding='utf-8-sig').splitlines()
    assert lines[:3] == ['AE_PARTS_V2',job.name,'OK'], lines[:5]
    width, height = map(int, lines[3].split())
    count = int(lines[4])
    at = 5
    input_im = Image.open(OUT/(job.name+'-input.png')).convert('RGBA')
    result_im = checker(input_im.size)
    kinds = []
    for i in range(count):
        row = lines[at].split(); at += 1
        kinds.append(row[0])
        mask = Image.new('L', (width,height)); md=ImageDraw.Draw(mask)
        for _ in range(int(row[3])):
            n = int(lines[at]); at += 1
            points=[]
            for __ in range(n):
                points.append(tuple(map(float,lines[at].split()))); at += 1
            md.polygon(points, fill=255)
        # Show actual worker polygons intersected with the synthetic input alpha.
        alpha = np.minimum(np.asarray(mask),np.asarray(input_im.getchannel('A')))
        color = tuple(round(c*255) for c in colorsys.hsv_to_rgb((i*.61803398875)%1,.60,.98))
        result_im.paste(Image.new('RGB', input_im.size, color), mask=Image.fromarray(alpha))
    assert at == len(lines)
    expected = 9 if job.name=='petals-mixed' else 6
    assert count == expected
    assert sorted(kinds)==sorted(['OBJECT']*6+(['TEXT']*3 if expected==9 else []))
    original=checker(input_im.size); original.paste(input_im,mask=input_im.getchannel('A'))
    rendered.append((original,result_im))
    results.append({'fixture':job.name, 'regions':count, 'classification':kinds, 'input_sha256':hashlib.sha256((OUT/(job.name+'-input.png')).read_bytes()).hexdigest()})

im, d = base('04  合成夹具：实际 worker 分区结果', '自绘 6 花瓣与 THE · 非用户素材 · 非 AE 真机截图', 1190)
text(d,(85,195),'输入：分离花瓣 / 混合文字',30,INK,True)
text(d,(855,195),'输出：实际区域蒙版着色',30,INK,True)
for i, pair in enumerate(rendered):
    y=260+i*410
    for j,picture in enumerate(pair):
        im.paste(picture.resize((680,405),Image.Resampling.NEAREST),(65+j*770,y))
    label='6 区：6 非文字（OBJECT）' if i==0 else '9 区：6 非文字 + 3 文字（TEXT）'
    text(d,(85,y+355),label,26,'white',True)
text(d,(68,1105),'区域颜色仅为可视化；TEXT 仍是图像蒙版，不是可编辑文字。',27,BLUE,True)
im.save(OUT/'synthetic-worker-regions.png')
manifest={'provenance':'Self-drawn geometric synthetic fixtures; actual worker extracted from root portable14 JSX. No user images read. Not an AE screenshot.', 'jsx_sha256':hashlib.sha256(JSX.read_bytes()).hexdigest(), 'cases':results}
(OUT/'synthetic-worker-results.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n', encoding='utf-8')
print(json.dumps(manifest,ensure_ascii=False,indent=2))
for path in sorted(OUT.glob('*.png')):
    with Image.open(path) as check:
        check.verify()
    print('IMAGE_OK',path.name)
