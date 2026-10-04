// Agnes AI 视频重生成脚本（agnes-video-2.5-flash, keyframe模式）
// 用法: node agnes_gen.mjs create seg1 | poll seg1 | download seg1 | status
// Key: 读取 /home/z/my-project/scripts/agnes_key.txt
import fs from 'fs';
import path from 'path';

const ROOT = '/home/z/my-project';
const KEY_FILE = `${ROOT}/scripts/agnes_key.txt`;
const BASE = process.env.AGNES_BASE || 'https://apihub.agnes-ai.cn/v1'; // 国内网关
const POLL_BASE = BASE.replace('/v1', '');
const MODEL = process.env.AGNES_MODEL || 'agnes-video-2.5-flash';
const KF = JSON.parse(fs.readFileSync(`${ROOT}/scripts/agnes_keyframes.json`, 'utf8'));
const OUT_DIR = `${ROOT}/download/战神归来视频v2`;
const TASK_DIR = `${ROOT}/scripts/agnes_tasks`;
fs.mkdirSync(OUT_DIR, { recursive: true });
fs.mkdirSync(TASK_DIR, { recursive: true });

const SEG_NAMES = {
  seg1: '片段一_雨夜之门', seg2: '片段二_订婚典礼', seg3: '片段三_当众退婚',
  seg4: '片段四_电话定局', seg5: '片段五_车海列阵', seg6: '片段六_龙王归位',
  seg7: '片段七_终局清算', seg8: '片段八_霸气收尾',
};

const SEGS = {
  seg1: {
    first: KF['EP01_CH01_SEG01_KF01_雨夜之门.png'],
    prompt: '雨夜电影感开场。镜头从雨幕中缓缓推近顾家大宅门前：@首帧 画面一致，男人黑色大衣被雨水打湿，身后豪车轮廓在雨中模糊。他缓步走向大门，每一步踩碎地面积水倒影，雨水顺伞沿连绵滴落。门口家丁撑伞相迎，男人抬手止住，自己收伞甩落水珠，仰头望向宅邸匾额，眼神从隐忍转为锋利，嘴角勾起冷笑。雷光一闪照亮侧脸。低沉雷声与雨声贯穿，脚步踏水声清晰。保持人物面部发型服装与首帧完全一致，雨夜冷蓝色调统一。禁止字幕、文字、水印。',
  },
  seg2: {
    first: KF['EP01_CH02_SEG02_KF01_订婚群像.png'],
    prompt: '豪华宴会厅订婚典礼现场，水晶吊灯金光璀璨。@首帧 画面一致：宾客群像环绕，顾念雪与宋家少爷立于中央高举香槟。镜头缓推穿过人群，宾客转身寒暄走动，侍者托盘穿行。顾念雪白色礼服裙摆轻转，笑容端庄，宋家少爷抬手与她十指相扣举杯。镜头摇至厅堂大门方向，仿佛察觉门外有视线。人群嗡嗡交谈声、杯盏轻碰声、弦乐四重奏背景贯穿。保持人物面部服装与首帧一致，金色暖调统一。禁止字幕、文字、水印。',
  },
  seg3: {
    first: KF['EP01_CH02_SEG03_KF01_入场亮相.png'],
    prompt: '宴会厅对峙戏。@首帧 画面一致：顾北辰墨色西装立于宴会厅大门内，正面朝向镜头，神情冷峻。他缓步走入厅中，脚步声让附近宾客纷纷回头、交头接耳向后避让。镜头随他横移，顾崇山皱眉拍案起身，顾念雪上前一步手指颤抖指向门口冷声呵斥，宋国豪不屑挥手，两名保安从侧门快步走来。顾北辰停在原地纹丝不动，目光扫过众人，嘴角缓缓扬起一丝讥诮笑意。窃窃私语声、急促脚步声、弦乐暗涌贯穿。人物面部服装全程一致，禁止字幕、文字、水印。',
  },
  seg4: {
    first: KF['EP01_CH02_SEG04_KF01_电话定局.png'],
    prompt: '宴会厅反杀戏。@首帧 画面一致：特写顾北辰修长手指从西装内袋取出一部黑色手机。拇指按下拨号键屏幕亮起冷光，他将手机缓缓举到耳边，低声说出一句话。全场一愣后爆发哄堂大笑，宾客交头接耳指指点点，顾念雪冷笑别过头，顾崇山与宋国豪举杯碰杯摇头讥笑。镜头缓缓推近顾北辰面部：他纹丝不动，眼神骤然转冷，眼中闪过锋芒，嘴角笑意加深。按键提示音、哄笑声、低音鼓点渐强贯穿。手机屏幕朝向人物，人物面部服装全程一致，禁止字幕、文字、水印。',
  },
  seg5: {
    first: KF['EP01_CH03_SEG05_KF01_车海列阵.png'],
    prompt: '夜色宴会厅外大道，黑压压豪车阵列震撼登场。@首帧 画面一致：数十辆黑色豪车车灯齐亮，镜头低角度贴地横移掠过一排排车轮与反光引擎盖，车灯依次点亮如潮水。保安与宾客在道旁目瞪口呆后退。领头车辆缓缓停下，车门被车外黑衣保镖拉开，皮鞋踏地，一只手扶住车门框。引擎低鸣、车门开合声、人群惊呼与快门声贯穿，冷蓝夜色调。保持车辆与人物风格与首帧一致。禁止字幕、文字、水印。',
  },
  seg6: {
    first: KF['EP01_CH03_SEG06_KF01_龙王归位.png'],
    last: KF['EP01_CH03_SEG06_KF02_全场震惊.png'],
    prompt: '宴会厅内权力反转高光时刻。@首帧 画面一致：顾北辰立于厅中气场全开，衣袂微动，身后黑衣手下鱼贯而入分列两侧躬身行礼，齐声低吼致意。镜头环绕他缓慢旋转上升，灯光在他身后打亮轮廓。随后镜头急速摇向全场：宾客面无人色纷纷后退撞翻酒杯，顾崇山踉跄扶住桌沿，顾念雪捂嘴色变，宋国豪瘫坐进椅子——最终画面落在@尾帧 全场震惊群像构图。齐声低吼、酒杯碎裂、人群惊呼、心跳鼓点贯穿。人物面部服装全程一致，禁止字幕、文字、水印。',
  },
  seg7: {
    first: KF['EP01_CH03_SEG07_KF01_终局清算.png'],
    prompt: '宴会厅终局清算。@首帧 画面一致：顾北辰缓步逼近瘫软的宋国豪与面色惨白的顾崇山，每一步都让两人向后瑟缩。他抬手将一份文件甩在宋国豪胸口，纸张飞散落地。顾念雪哭着上前想抓他衣角，被他侧身避开，他俯身在她耳边低语一句，她如遭雷击踉跄后退。镜头围绕两人缓慢旋转，宾客屏息围观无人敢言。纸张散落声、高跟鞋后退声、压抑弦乐贯穿。人物面部服装全程一致，禁止字幕、文字、水印。',
  },
  seg8: {
    first: KF['EP01_CH04_SEG08_KF01_霸气收尾.png'],
    prompt: '霸气收尾镜头。@首帧 画面一致：顾北辰背手立于宴会厅中央高处，逆光轮廓。他缓缓转身面向镜头，抬手整理袖扣，身后手下同步躬身。镜头从下向上缓推至面部特写：他眼神睥睨扫过全场，嘴角勾起王者般的冷笑，一挥手，大门外豪车灯光依次亮起。他迈步向镜头走来，画面在半身特写定格收黑。沉稳脚步声、袖扣轻响、豪车引擎齐鸣、磅礴史诗配乐渐强贯穿。人物面部服装与首帧一致，禁止字幕、文字、水印。',
  },
};

function headers() {
  return { 'Authorization': `Bearer ${fs.readFileSync(KEY_FILE, 'utf8').trim()}`, 'Content-Type': 'application/json' };
}
function taskFile(seg) { return `${TASK_DIR}/${seg}.json`; }
function loadTask(seg) { try { return JSON.parse(fs.readFileSync(taskFile(seg), 'utf8')); } catch { return null; } }
function saveTask(seg, t) { fs.writeFileSync(taskFile(seg), JSON.stringify(t, null, 1)); }

async function create(seg) {
  const s = SEGS[seg];
  if (!s) throw new Error('unknown seg ' + seg);
  const body = {
    model: MODEL,
    prompt: s.prompt,
    mode: 'keyframe',
    seconds: process.env.AGNES_SECONDS || '10',
    size: '720P',
    aspect_ratio: '9:16',
    first_frame: s.first,
  };
  if (s.last) body.last_frame = s.last;
  const r = await fetch(`${BASE}/videos`, { method: 'POST', headers: headers(), body: JSON.stringify(body) });
  const txt = await r.text();
  let j; try { j = JSON.parse(txt); } catch { j = { raw: txt.slice(0, 500) }; }
  if (!r.ok) { console.error(`[${seg}] create HTTP ${r.status}:`, txt.slice(0, 400)); process.exit(1); }
  const rec = { seg, name: SEG_NAMES[seg], created_at: Date.now(), create_resp: j, video_id: j.video_id || j.id || j.task_id, polls: [] };
  saveTask(seg, rec);
  console.log(`[${seg}] created video_id=${rec.video_id} status=${j.status}`);
}

async function poll(seg) {
  const t = loadTask(seg);
  if (!t || !t.video_id) { console.error(`[${seg}] no task`); process.exit(1); }
  const url = `${POLL_BASE}/agnesapi?video_id=${encodeURIComponent(t.video_id)}&model_name=${encodeURIComponent(MODEL)}`;
  const r = await fetch(url, { headers: headers() });
  const txt = await r.text();
  let j; try { j = JSON.parse(txt); } catch { j = { raw: txt.slice(0, 300) }; }
  const entry = { at: Date.now(), http: r.status, status: j.status, progress: j.progress, err: j.error || j.detail || null, url: j.metadata?.url || null };
  t.polls.push(entry);
  if (r.ok) { t.latest = j; }
  saveTask(seg, t);
  console.log(`[${seg}] HTTP ${r.status} status=${j.status} progress=${j.progress ?? '-'} url=${entry.url || '-'}`);
  if (entry.url) console.log(`[${seg}] READY -> ${entry.url}`);
}

async function download(seg) {
  const t = loadTask(seg);
  const url = t?.latest?.metadata?.url;
  if (!url) { console.error(`[${seg}] no url yet`); process.exit(1); }
  const out = `${OUT_DIR}/${SEG_NAMES[seg]}.mp4`;
  const r = await fetch(url);
  if (!r.ok) { console.error(`[${seg}] download HTTP ${r.status}`); process.exit(1); }
  fs.writeFileSync(out, Buffer.from(await r.arrayBuffer()));
  console.log(`[${seg}] saved ${out} (${(fs.statSync(out).size / 1048576).toFixed(1)} MB)`);
}

const [cmd, seg] = process.argv.slice(2);
if (cmd === 'create' && seg) await create(seg);
else if (cmd === 'poll' && seg) await poll(seg);
else if (cmd === 'download' && seg) await download(seg);
else if (cmd === 'status') {
  for (const seg of Object.keys(SEGS)) {
    const t = loadTask(seg);
    if (!t) { console.log(`[${seg}] -`); continue; }
    const p = t.polls.at(-1);
    console.log(`[${seg}] video_id=${t.video_id} last: status=${p?.status} progress=${p?.progress} url=${p?.url ? 'YES' : 'no'}`);
  }
} else { console.log('usage: node agnes_gen.mjs create|poll|download <seg1..8> | status'); }
