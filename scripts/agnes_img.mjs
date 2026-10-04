// Agnes图像生成(单argv管道格式): node agnes_img.mjs "prompt|输出文件.png|1080x1920"
import fs from 'fs';
const KEY = fs.readFileSync('/home/z/my-project/scripts/agnes_key.txt', 'utf8').trim();
const [prompt, outFile, size = '1080x1920'] = process.argv[2].split('|');
const r = await fetch('https://apihub.agnes-ai.cn/v1/images/generations', {
  method: 'POST',
  headers: { 'Authorization': `Bearer ${KEY}`, 'Content-Type': 'application/json' },
  body: JSON.stringify({ model: 'agnes-image-2.5-flash', prompt, size, n: 1 }),
});
const j = await r.json();
if (!r.ok || !j.data?.[0]?.url) { console.error('FAIL', r.status, JSON.stringify(j).slice(0, 200)); process.exit(1); }
const img = await fetch(j.data[0].url);
fs.writeFileSync(outFile, Buffer.from(await img.arrayBuffer()));
console.log('OK', outFile, (fs.statSync(outFile).size / 1024).toFixed(0) + 'KB');
