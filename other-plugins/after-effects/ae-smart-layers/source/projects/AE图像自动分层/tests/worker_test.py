"""Portable14 real-image, petal objects, mixed content and native-entry regressions."""
from pathlib import Path
import json, subprocess, tempfile, time, colorsys, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont, ImageChops
ROOT=Path(__file__).resolve().parents[3]
PS=r'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe'
WORK=Path(tempfile.mkdtemp(prefix='ae_parts_portable14_tests_'))
source=(ROOT/'zexp_image_split_portable.jsx').read_text(encoding='utf-8-sig')
block=source.split('var WORKER = [',1)[1].split('].join("\\n");',1)[0]
worker='\n'.join(json.loads(line.strip().rstrip(',')) for line in block.splitlines() if line.strip())
embedded=worker.split("$code = @'\n",1)[1].split("\n'@",1)[0]
canonical=(Path(__file__).parent/'..'/'worker.cs').read_text(encoding='utf-8-sig').rstrip('\n')
templates=(Path(__file__).parent/'..'/'glyph_templates.cs').read_text(encoding='utf-8-sig').rstrip('\n')
templates='\n'.join(line for line in templates.splitlines() if not line.startswith('using '))
assert embedded==canonical+'\n'+templates,'Rebuild JSX before testing: embedded worker differs from canonical source'
code=embedded
assert 'StrokeLabels' not in code and 'mode.txt' not in code
cases=[]
public_mode='--public' in sys.argv
skipped_private=[]
def add(name,image,count,kind=None,foreground=None):
    job=WORK/name;job.mkdir();image.save(job/'input.png')
    if foreground is not None:foreground.save(job/'foreground.png')
    (job/'source.txt').write_text(str(job/'input.png'),encoding='utf-8-sig')
    cases.append((job,count,kind))
def drawing(size=(160,100)):
    im=Image.new('RGBA',size);return im,ImageDraw.Draw(im)
def petal_scene(background,orientations,gradient=False,with_text=False):
    size=(420,250);im=Image.new('RGBA',size,background)
    if gradient:
        yy,xx=np.indices((size[1],size[0]));bg=np.empty((size[1],size[0],4),dtype=np.uint8);bg[:,:,:3]=np.stack(((35+xx//3)%256,(55+yy//3)%256,(95+(xx+yy)//5)%256),axis=2);bg[:,:,3]=255;im=Image.fromarray(bg,'RGBA')
    colors=[(250,70,85),(40,170,245),(245,185,25),(110,220,95),(185,75,220),(255,125,25)]
    positions=[(65,65),(210,65),(355,65),(65,185),(210,185),(355,185)]
    for i,angle in enumerate(orientations):
        px,py=positions[i%len(positions)];petal=Image.new('L',size);pd=ImageDraw.Draw(petal);pd.ellipse((px-12,py-34,px+12,py+34),fill=255);petal=petal.rotate(float(angle),resample=Image.Resampling.BICUBIC,center=(px,py));alpha=np.asarray(petal)>0;arr=np.asarray(im).copy()
        if gradient:
            yy,xx=np.indices((size[1],size[0]));rgb=np.stack(((colors[i%len(colors)][0]+xx//5)%256,(colors[i%len(colors)][1]+yy//5)%256,(colors[i%len(colors)][2]+(xx+yy)//10)%256),axis=2);arr[alpha,:3]=rgb[alpha]
        else:arr[alpha,:3]=colors[i%len(colors)]
        arr[alpha,3]=255;im=Image.fromarray(arr,'RGBA')
    if with_text:
        d=ImageDraw.Draw(im);font=ImageFont.truetype(str(Path(r'C:\\Windows\\Fonts\\arial.ttf')),23);d.text((155,110),'THE',font=font,fill=(245,245,245,255))
    return im
def foreground_components(image):
    mask=np.asarray(image.getchannel('A'))>=8;seen=np.zeros(mask.shape,dtype=bool);height,width=mask.shape;count=0
    for y,x in zip(*np.nonzero(mask)):
        if seen[y,x]:continue
        area=0;queue=[(int(x),int(y))];seen[y,x]=True
        while queue:
            px,py=queue.pop();area+=1
            for dy in (-1,0,1):
                for dx in (-1,0,1):
                    xx,yy=px+dx,py+dy
                    if 0<=xx<width and 0<=yy<height and mask[yy,xx] and not seen[yy,xx]:seen[yy,xx]=True;queue.append((xx,yy))
        if area>=16:count+=1 # Ignore bicubic rotation's isolated 1-pixel ringing, not subject bodies.
    return count
# Synthetic separated petal objects and mixed text exercise alpha, background and color independence.
petal_directions=[-90,-35,25,80,145,205]
assert foreground_components(petal_scene((0,0,0,0),petal_directions))==6,'Petal fixture must contain six genuinely separate subjects'
assert foreground_components(petal_scene((0,0,0,0),petal_directions,with_text=True))==9,'Mixed fixture must keep six petals and THE separate'
add('petals_transparent',petal_scene((0,0,0,0),petal_directions),6,'OBJECT')
add('petals_solid',petal_scene((30,35,42,255),petal_directions),6,'OBJECT')
add('petals_gradient',petal_scene((30,35,42,255),petal_directions,gradient=True),6,'OBJECT',foreground=petal_scene((0,0,0,0),petal_directions).getchannel('A'))
add('petals_mixed_text',petal_scene((0,0,0,0),petal_directions,with_text=True),9)
# Preserve real connections within a single glyph.
for sigma in (0,1.5,3):
    im,d=drawing();d.rectangle((22,15,44,84),fill='white');d.rectangle((110,15,132,84),fill='white');d.rectangle((22,47,132,51),fill='white')
    add(f'bridge_{sigma}',im.filter(ImageFilter.GaussianBlur(sigma)),1)
for sigma in (0,2,5):
    for alpha in (60,255):
        im,d=drawing((220,100));d.rectangle((20,15,65,84),fill=(255,255,255,alpha));d.rectangle((150,15,195,84),fill=(255,255,255,alpha))
        add(f'objects_{sigma}_{alpha}',im.filter(ImageFilter.GaussianBlur(sigma)),2,'OBJECT')
for background,foreground in (('white','black'),('#303030','#888888'),('#d4b489','#765229')):
    im=Image.new('RGBA',(220,100),background);d=ImageDraw.Draw(im);d.rectangle((20,15,65,84),fill=foreground);d.rectangle((150,15,195,84),fill=foreground)
    add('opaque_'+background.replace('#',''),im.filter(ImageFilter.GaussianBlur(2)),2,'OBJECT')
im,d=drawing((220,100));d.rectangle((20,15,65,84),fill=(255,255,255,60));d.rectangle((150,15,195,84),fill='white')
add('unequal_alpha',im.filter(ImageFilter.GaussianBlur(2)),2,'OBJECT')
im,d=drawing((260,120));d.polygon([(10,100),(60,15),(110,100)],fill='white');d.polygon([(150,40),(185,15),(225,40),(215,100),(155,100)],fill='white')
add('irregular_icons',im.filter(ImageFilter.GaussianBlur(1.5)),2)
im,d=drawing((300,100))
for x in (15,110,205):d.ellipse((x,20,x+65,80),fill='white')
add('aligned_disks',im.filter(ImageFilter.GaussianBlur(2)),3,'OBJECT')
im,d=drawing((100,100));d.polygon([(50,8),(62,35),(92,37),(70,57),(78,88),(50,73),(22,88),(30,57),(8,37),(38,35)],fill='white')
add('star',im.filter(ImageFilter.GaussianBlur(1)),1)
fonts=[Path(r'C:\Windows\Fonts\arial.ttf'),Path(r'C:\Windows\Fonts\times.ttf'),Path(r'C:\Windows\Fonts\simhei.ttf')]
for fontpath in fonts:
    assert fontpath.exists(),fontpath
    font=ImageFont.truetype(str(fontpath),70)
    letters=('m','W','N','H','i','j','İ') if fontpath.stem!='simhei' else ('田','中','永')
    for letter in letters:
        for sigma in (0,1.7):
            im,d=drawing((150,110));d.text((15,5),letter,font=font,fill='white')
            add(f'font_{fontpath.stem}_{ord(letter)}_{sigma}',im.filter(ImageFilter.GaussianBlur(sigma)),1)
for name,count in (('the-third-gold',8),('the-third-blur',8),('second-blur',6)):
    if public_mode:
        skipped_private.append(name)
        continue
    actual=Image.open(Path(__file__).parent/'fixtures'/(name+'.png')).convert('RGBA')
    add(name,actual,count,'TEXT')
    if name=='second-blur':
        add(name+'_scale',actual.resize((673,153),Image.Resampling.LANCZOS),6,'TEXT')
        add(name+'_extra_blur',actual.filter(ImageFilter.GaussianBlur(1.5)),6,'TEXT')
for fontpath in fonts[:2]:
    font=ImageFont.truetype(str(fontpath),66)
    for word in ('mWm','THE THIRD','HHH','WORLD','MOM','NEXT LEVEL','WiFi','SECOND','SCREEN','ONCE'):
        for sigma in (1.5,3):
            im,d=drawing((520,110));d.text((12,5),word,font=font,fill='white')
            add('word_'+fontpath.stem+'_'+word.replace(' ','_')+'_'+str(sigma),im.filter(ImageFilter.GaussianBlur(sigma)),len(word.replace(' ','')))
add('cancelled',im,None);(WORK/'cancelled'/'cancel.txt').write_text('cancel')
add('empty',Image.new('RGBA',(100,100)),None)
if '--objects-only' in sys.argv:cases=[case for case in cases if case[0].name.startswith('petals_')]
harness=WORK/'run.ps1'
harness.write_text("$ErrorActionPreference='Stop'\nAdd-Type -AssemblyName System.Drawing\n$code=@'\n"+code+"\n'@\nAdd-Type -TypeDefinition $code -ReferencedAssemblies System.Drawing\n"+'\n'.join("[SplitWorker]::Run('"+str(job).replace("'","''")+"')" for job,_,_ in cases),encoding='utf-8-sig')
started=time.monotonic()
subprocess.run([PS,'-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',str(harness)],check=True,timeout=240)
failures=[];previews=[];actual_counts={};classification={}
for job,expected,expected_kind in cases:
    lines=(job/'result.txt').read_text(encoding='utf-8-sig').splitlines()
    assert lines[:2]==['AE_PARTS_V2',job.name]
    if expected is None:
        assert lines[2]=='ERROR' and len(lines)>3
        print('PASS',job.name,'safe rejection');continue
    if lines[2]!='OK':failures.append((job.name,'worker error',lines[3:]));continue
    retained,assigned=map(int,(job/'coverage.txt').read_text().split());assert retained==assigned and assigned>0
    width,height=map(int,lines[3].split());count=int(lines[4]);at=5
    union=Image.new('L',(width,height));preview=Image.new('RGB',(width,height),'#202020');kinds=[]
    palette=[tuple(int(255*c) for c in colorsys.hsv_to_rgb((i*.61803398875)%1,.58,.96)) for i in range(40)]
    for part_index in range(count):
        row=lines[at].split();at+=1;assert len(row)==4
        kind=row[0];kinds.append(kind);assert kind in ('TEXT','OBJECT','UNKNOWN')
        feather,padding,loops=map(float,row[1:]);assert 0<=feather<=64 and 0<=padding<=256 and 1<=loops<=8
        part_mask=Image.new('L',(width,height));pd=ImageDraw.Draw(part_mask)
        for _ in range(int(loops)):
            n=int(lines[at]);at+=1;assert 3<=n<=256
            points=[]
            for __ in range(n):
                x,y=map(float,lines[at].split());at+=1;assert 0<=x<=width and 0<=y<=height;points.append((x,y))
            pd.polygon(points,fill=255);ImageDraw.Draw(preview).polygon(points,fill=palette[part_index])
        radius=int(padding/2**.5)
        if radius:part_mask=part_mask.filter(ImageFilter.MaxFilter(2*radius+1))
        union=ImageChops.lighter(union,part_mask)
    assert at==len(lines)
    im=Image.open(job/'input.png').convert('RGBA');pixels=np.asarray(im).astype(float)
    truth=job/'foreground.png'
    signal=np.asarray(Image.open(truth).convert('L')).astype(float) if truth.exists() else pixels[:,:,3] if pixels[:,:,3].min()<255 else abs(pixels[:,:,:3]-pixels[0,0,:3]).max(axis=2)
    strong=signal>=max(1,signal.max()*.65)
    lost=float(np.count_nonzero(strong & (np.asarray(union)==0)))/max(1,np.count_nonzero(strong))
    if lost>.005:failures.append((job.name,'strong foreground lost',round(lost,4)))
    if count!=expected:failures.append((job.name,count,expected))
    if expected_kind and any(k!=expected_kind for k in kinds):failures.append((job.name,'types',kinds,expected_kind))
    if job.name=='petals_mixed_text' and sorted(kinds)!=sorted(['OBJECT']*6+['TEXT']*3):failures.append((job.name,'mixed types',kinds))
    if job.name.startswith(('the-third','second','petals_')):
        actual_counts[job.name]=count;classification[job.name]=kinds
    if job.name.startswith(('the-third','second','petals_')) or job.name in ('aligned_disks','irregular_icons','star'):
        tinted=Image.new('RGB',im.size,'#202020');alpha=Image.fromarray(np.minimum(255,signal/max(1,signal.max())*255).astype('uint8'));tinted.paste(preview,mask=alpha)
        previews.append((job.name,im,tinted,kinds))
    print('CASE',job.name,'parts',count,'types',','.join(kinds))
native=WORK/'native_second';native.mkdir()
native_source='word_arial_SECOND_1.5' if public_mode else 'second-blur'
(native/'worker.ps1').write_text(worker,encoding='utf-8-sig');(native/'source.txt').write_text(str(WORK/native_source/'input.png'),encoding='utf-8-sig')
native_started=time.monotonic();subprocess.run([PS,'-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',str(native/'worker.ps1')],check=True,timeout=55)
assert (native/'started.txt').exists()
native_lines=(native/'result.txt').read_text(encoding='utf-8-sig').splitlines()
assert native_lines[:3]==['AE_PARTS_V2','native_second','OK'] and int(native_lines[4])==6,native_lines[:6]
native_seconds=round(time.monotonic()-native_started,3);assert native_seconds<45
montage=Image.new('RGB',(2000,len(previews)*245),'#202020');md=ImageDraw.Draw(montage)
for row,(name,original,preview,kinds) in enumerate(previews):
    md.text((8,row*245+3),name+' | '+','.join(kinds),fill='white')
    for col,image in enumerate((original,preview)):
        image=image.copy();image.thumbnail((990,215));montage.paste(image,(col*1000+5,row*245+25),image.getchannel('A') if image.mode=='RGBA' else None)
montage.save(WORK/'actual_regions.png')
report={'public_mode':public_mode,'skipped_private':skipped_private,'native_source':native_source,'cases':len(cases),'protocol':'AE_PARTS_V2','actual_counts':actual_counts,'classification':classification,'native_second_seconds':native_seconds,'failed':failures,'seconds':round(time.monotonic()-started,3),'artifacts':str(WORK)}
(WORK/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8');print(json.dumps(report,ensure_ascii=False,indent=2));assert not failures,failures
