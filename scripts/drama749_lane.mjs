// 《749收容》双车道并行视频编排器(重建版,含全部已修坑)
// Lane A: agnes-video-2.5-flash (mode=keyframe单数,原生竖屏704x1280)
// Lane B: agnes-video-v2.0 (mode=ti2vid+image数组,固定1088x832横屏,剪辑时模糊垫底)
// Lane C: agnes-video-2.5 弃用(403配额$0)
// 用法: node drama749_lane.mjs images|loop|once|status|qa <id>
import fs from 'fs';
import path from 'path';
import { execSync } from 'child_process';
import { ASSETS } from './drama749_assets.mjs';
import { EP1 } from './drama749_ep1.mjs';
import { EP2 } from './drama749_ep2.mjs';

const ROOT = '/home/z/my-project';
const OUT = `${ROOT}/download/749收容漫剧`;
const KEY_FILE = `${ROOT}/scripts/agnes_key.txt`;
const BASE = 'https://apihub.agnes-ai.cn/v1';
const POLL_BASE = 'https://apihub.agnes-ai.cn';
const IMG_MODEL = 'agnes-image-2.5-flash';
const TASK_DIR = `${OUT}/_任务状态`;
const LANES_FILE = `${TASK_DIR}/_lanes.json`;
const LOG = `${ROOT}/scripts/drama749_lane.log`;
fs.mkdirSync(TASK_DIR, { recursive: true });

const SHOTS = [...EP1, ...EP2];
const shotFile = (id) => `${OUT}/05_动态片段/第${id[2]}集_镜${id.slice(4)}.mp4`;
const boardFile = (id) => `${OUT}/04_分镜成图/${id}.png`;
const LANES = [
  { key: 'flash', model: 'agnes-video-2.5-flash', body: 'keyframe' },
  { key: 'v20', model: 'agnes-video-v2.0', body: 'ti2vid' },
];
const log = (m) => { const line = `[${new Date().toISOString().slice(5, 16).replace('T', ' ')}] ${m}`; console.log(line); fs.appendFileSync(LOG, line + '\n'); };
const headers = () => ({ 'Authorization': `Bearer ${fs.readFileSync(KEY_FILE, 'utf8').trim()}`, 'Content-Type': 'application/json' });
const sleep = (ms) => new Promise(r => setTimeout(r, ms));
const loadLanes = () => { try { return JSON.parse(fs.readFileSync(LANES_FILE, 'utf8')); } catch { return { disabled: {} }; } };
const saveLanes = (l) => fs.writeFileSync(LANES_FILE, JSON.stringify(l, null, 1));
const loadTask = (id) => { try { return JSON.parse(fs.readFileSync(`${TASK_DIR}/${id}.json`, 'utf8')); } catch { return null; } };
const saveTask = (id, t) => fs.writeFileSync(`${TASK_DIR}/${id}.json`, JSON.stringify(t, null, 1));
const IMGURLS = `${TASK_DIR}/imgurls.json`;
const loadImgUrls = () => { try { return JSON.parse(fs.readFileSync(IMGURLS, 'utf8')); } catch { return {}; } };
const saveImgUrls = (m) => fs.writeFileSync(IMGURLS, JSON.stringify(m, null, 1));

function ffprobeWH(file) {
  try {
    const out = execSync(`ffprobe -v error -select_streams v:0 -show_entries stream=width,height -of csv=p=0 "${file}"`).toString().trim();
    const [w, h] = out.split(',').map(Number);
    return { w, h };
  } catch { return { w: 0, h: 0 }; }
}

async function genImage(prompt, file) {
  for (let attempt = 0; attempt < 8; attempt++) {
    const r = await fetch(`${BASE}/images/generations`, {
      method: 'POST', headers: headers(),
      body: JSON.stringify({ model: IMG_MODEL, prompt, size: '1080x1920', n: 1 }),
    });
    const txt = await r.text();
    if (r.status === 429) { const w = 45 + attempt * 15; log(`[img] 429 backoff ${w}s`); await sleep(w * 1000); continue; }
    if (!r.ok) throw new Error(`img HTTP ${r.status}: ${txt.slice(0, 200)}`);
    const j = JSON.parse(txt);
    const url = j.data?.[0]?.url;
    if (!url) throw new Error('no url: ' + txt.slice(0, 150));
    const img = await fetch(url);
    if (!img.ok) throw new Error(`dl HTTP ${img.status}`);
    fs.mkdirSync(path.dirname(file), { recursive: true });
    fs.writeFileSync(file, Buffer.from(await img.arrayBuffer()));
    return url;
  }
  throw new Error('img retries exhausted');
}

async function cmdImages() {
  const urls = loadImgUrls();
  const jobs = [
    ...ASSETS.map(a => ({ id: a.id, file: `${OUT}/${a.file}`, prompt: a.prompt })),
    ...SHOTS.map(s => ({ id: s.id, file: boardFile(s.id), prompt: s.img })),
  ];
  const todo = jobs.filter(j => !fs.existsSync(j.file));
  log(`images: ${jobs.length - todo.length}/${jobs.length} exist, todo=${todo.length}`);
  for (const j of todo) {
    try {
      const url = await genImage(j.prompt, j.file);
      urls[j.id] = url; saveImgUrls(urls);
      log(`[img] OK ${j.id}`);
    } catch (e) { log(`[img] FAIL ${j.id}: ${e.message}`); }
    await sleep(12000);
  }
  log('images done');
}

function shotNeedsWork(id) {
  if (fs.existsSync(shotFile(id))) return false;
  const t = loadTask(id);
  if (!t) return true;
  if (t.verdict === 'REJECT') return true;
  if (t.status === 'FAILED') return true;
  if (!t.video_id) return true;
  return false;
}

function laneBusy(model) {
  for (const s of SHOTS) {
    const t = loadTask(s.id);
    if (t && t.model === model && t.video_id && !t.url && t.status !== 'FAILED' && t.verdict !== 'REJECT') return t.id;
  }
  return null;
}

function nextShotFor(model, urls) {
  const isFlash = model === 'agnes-video-2.5-flash';
  const redo = [], fresh = [];
  for (const s of SHOTS) {
    if (!shotNeedsWork(s.id)) continue;
    if (!urls[s.id]) continue;
    const t = loadTask(s.id);
    if (t && (t.status === 'FAILED' || t.verdict === 'REJECT')) { if (isFlash) redo.push(s); }
    else fresh.push(s);
  }
  return redo[0] || fresh[0] || null;
}

async function submit(shot, lane, urls) {
  const bodies = [];
  if (lane.body === 'keyframe') {
    bodies.push({ model: lane.model, prompt: shot.vid, mode: 'keyframe', seconds: String(shot.sec), size: '720P', aspect_ratio: '9:16', first_frame: urls[shot.id] });
  } else {
    bodies.push({ model: lane.model, prompt: shot.vid, mode: 'ti2vid', image: [urls[shot.id]], seconds: String(shot.sec), size: '720P' });
  }
  for (let bi = 0; bi < bodies.length; bi++) {
    for (let attempt = 0; attempt < 6; attempt++) {
      let r, txt;
      try {
        r = await fetch(`${BASE}/videos`, { method: 'POST', headers: headers(), body: JSON.stringify(bodies[bi]) });
        txt = await r.text();
      } catch (e) { log(`[${lane.key}] ${shot.id} fetch err ${e.message}`); await sleep(20000); continue; }
      if (r.status === 429) { if (attempt === 0) { log(`[${lane.key}] ${shot.id} 429, 40s后单次重试`); await sleep(40000); continue; } log(`[${lane.key}] ${shot.id} 429 x2, 本轮让出`); return false; }
      if (r.status === 503 || txt.includes('queue is full') || txt.includes('queue full')) { log(`[${lane.key}] ${shot.id} queue full, 本轮让出`); return false; }
      if (!r.ok) { log(`[${lane.key}] ${shot.id} body#${bi} HTTP ${r.status}: ${txt.slice(0, 180)}`); break; }
      try {
        const j = JSON.parse(txt);
        const vid = j.video_id || j.id || j.task_id;
        if (!vid) { log(`[${lane.key}] ${shot.id} no video_id: ${txt.slice(0, 180)}`); break; }
        saveTask(shot.id, { id: shot.id, model: lane.model, lane: lane.key, video_id: vid, created_at: Date.now(), status: 'submitted', polls: [], url: null });
        log(`[${lane.key}] CREATED ${shot.id} -> ${String(vid).slice(-24)} (body#${bi})`);
        return true;
      } catch (e) { log(`[${lane.key}] ${shot.id} resp parse err`); break; }
    }
  }
  return false;
}

async function downloadOne(shot, t) {
  const out = shotFile(shot.id);
  if (fs.existsSync(out)) return 'exists';
  const r = await fetch(t.url);
  if (!r.ok) { log(`[dl] ${shot.id} HTTP ${r.status}`); return 'httperr'; }
  const buf = Buffer.from(await r.arrayBuffer());
  if (buf.length < 100 * 1024) { log(`[dl] ${shot.id} too small, REJECT`); t.verdict = 'REJECT'; t.verdictNote = 'too small'; saveTask(shot.id, t); return 'reject'; }
  fs.mkdirSync(path.dirname(out), { recursive: true });
  fs.writeFileSync(out, buf);
  const { w, h } = ffprobeWH(out);
  log(`[dl] SAVED ${shot.id} (${(buf.length / 1048576).toFixed(1)}MB ${w}x${h})`);
  if (w > h) { t.verdict = 'OK-LANDSCAPE'; t.verdictNote = `${w}x${h} 剪辑垫底`; saveTask(shot.id, t); log(`[qa] ${shot.id} 横屏→接受,后期垫底`); }
  return 'ok';
}

async function pollAll() {
  let pending = 0, ready = 0, failed = 0;
  for (const s of SHOTS) {
    const t = loadTask(s.id);
    if (!t?.video_id) continue;
    if (t.verdict === 'REJECT') continue;
    if (t.url) { try { const r = await downloadOne(s, t); if (r === 'ok' || r === 'exists') ready++; } catch (e) { log(`[dl-catch] ${s.id} ${e.message}`); } continue; }
    try {
      const r = await fetch(`${POLL_BASE}/agnesapi?video_id=${encodeURIComponent(t.video_id)}&model_name=${encodeURIComponent(t.model)}`, { headers: headers() });
      const txt = await r.text();
      let j; try { j = JSON.parse(txt); } catch { j = {}; }
      const status = j.status || '?';
      const url = j.metadata?.url || j.url || null;
      t.polls.push({ at: Date.now(), status, progress: j.progress ?? null });
      if (url && status !== 'FAILED') {
        t.url = url; t.latest = j; saveTask(s.id, t);
        log(`[poll] READY ${s.id} [${t.lane}]`);
        const dr = await downloadOne(s, t);
        if (dr === 'ok' || dr === 'exists') ready++; else if (dr === 'reject') failed++;
      } else if (status === 'FAILED') {
        t.status = 'FAILED'; t.err = (j.error || j.detail || '').toString().slice(0, 300); saveTask(s.id, t); failed++;
        log(`[poll] FAILED ${s.id}: ${t.err.slice(0, 80)}`);
      } else { saveTask(s.id, t); pending++; }
    } catch (e) { pending++; }
    await sleep(300);
  }
  return { pending, ready, failed };
}

async function cycle() {
  const L = loadLanes();
  const urls = loadImgUrls();
  // 双车道并行领活(互不阻塞)
  await Promise.all(LANES.filter(l => !L.disabled[l.key]).map(async lane => {
    try {
      if (laneBusy(lane.model)) return;
      const shot = nextShotFor(lane.model, urls);
      if (!shot) return;
      await submit(shot, lane, urls);
    } catch (e) { log(`[${lane.key}] submit err ${e.message}`); }
  }));
  const r = await pollAll();
  const done = SHOTS.filter(s => fs.existsSync(shotFile(s.id))).length;
  const inFlight = SHOTS.filter(s => { const t = loadTask(s.id); return t?.video_id && !t.url && t.status !== 'FAILED' && t.verdict !== 'REJECT'; }).length;
  log(`[cycle] disk=${done}/${SHOTS.length} inFlight=${inFlight} poll(pend=${r.pending} ready=${r.ready} fail=${r.failed}) lanes=${LANES.map(l => L.disabled[l.key] ? '×' + l.key : l.key).join(',')}`);
  return done;
}

async function loop() {
  log('=== 双车道编排器启动 ===');
  const t0 = Date.now();
  while (Date.now() - t0 < 500 * 1000) {
    try {
      const done = await cycle();
      if (done >= SHOTS.length) { log('=== 全部37镜落盘 ==='); break; }
    } catch (e) { log(`[loop] err ${e.message}`); }
    await sleep(25000);
  }
}

function cmdStatus() {
  const L = loadLanes();
  const urls = loadImgUrls();
  let disk = 0;
  for (const s of SHOTS) {
    const t = loadTask(s.id);
    const v = fs.existsSync(shotFile(s.id));
    if (v) disk++;
    const st = v ? 'DISK' : !t ? (!urls[s.id] ? 'NOIMG' : 'NONE') : t.verdict === 'REJECT' ? `REJECT(${t.verdictNote})` : t.url ? 'READY' : t.status === 'FAILED' ? 'FAILED' : `${t.status}(${t.polls.length})`;
    console.log(`${s.id} [${t?.lane || '-'}] ${st}${t?.err ? ' ' + t.err.slice(0, 50) : ''}`);
  }
  console.log(`disk=${disk}/${SHOTS.length} lanes: ${LANES.map(l => L.disabled[l.key] ? '×' + l.key : '√' + l.key).join(' ')}`);
}

const cmd = process.argv[2];
process.on('unhandledRejection', (e) => log(`[unhandledRejection] ${e?.message || e}`));
process.on('uncaughtException', (e) => log(`[uncaughtException] ${e?.message || e}`));
if (cmd === 'images') await cmdImages();
else if (cmd === 'loop') await loop();
else if (cmd === 'once') await cycle();
else if (cmd === 'status') cmdStatus();
else if (cmd === 'qa') { const f = shotFile(process.argv[3]); const { w, h } = ffprobeWH(f); execSync(`ffmpeg -y -v error -i "${f}" -vf "select=eq(n\\,0)" -vframes 1 /tmp/qa.png`); console.log(`${f} ${w}x${h}`); }
else console.log('usage: node drama749_lane.mjs images|loop|once|status|qa <id>');
