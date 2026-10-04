#!/usr/bin/env python3
# 《749收容》SRT字幕生成：按实际片段时长平铺台词
import json, subprocess

OUT = '/home/z/my-project/download/749收容漫剧'
SHOTS = json.load(open('/home/z/my-project/scripts/drama749_shots_subs.json'))

def dur(p):
    r = subprocess.run(['ffprobe','-v','error','-show_entries','format=duration','-of','csv=p=0',p],capture_output=True,text=True)
    try: return float(r.stdout.strip())
    except: return 5.0

def ts(t):
    ms = int(round(t*1000)); h,ms = divmod(ms,3600000); m,ms = divmod(ms,60000); s,ms = divmod(ms,1000)
    return f'{h:02d}:{m:02d}:{s:02d},{ms:03d}'

for ep in (1,2):
    items = [s for s in SHOTS if s['ep']==ep]
    cum = 0.0; out=[]; idx=1
    for it in items:
        f = f"{OUT}/05_动态片段/第{ep}集_镜{it['no']:03d}.mp4"
        d = dur(f)
        cues = it.get('subs') or []
        k = len(cues)
        if k:
            span = d/k
            for j,c in enumerate(cues):
                st = cum + j*span
                en = cum + (j+1)*span - 0.05
                if en-st < 0.6: en = st+0.6
                out.append(f'{idx}\n{ts(st)} --> {ts(en)}\n{c}\n'); idx+=1
        cum += d
    open(f'{OUT}/06_音频/ep{ep}.srt','w').write('\n'.join(out))
    print(f'ep{ep}.srt: {idx-1} cues, total {cum:.1f}s')
print('done')
