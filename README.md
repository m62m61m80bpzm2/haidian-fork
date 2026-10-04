# 749收容漫剧 · 最终交付与全管线备份

《大一实习，你跑去749收容怪物》第1-2集竖屏漫剧，37镜全部出片，两集成片验收通过。

## 内容地图

```
成品分卷/
  749收容漫剧_最终交付.zip.part-00 ~ 02   # 完整交付包(254MB)切成的3个分卷(均<100MB防GitHub拒收)
rejoin.sh                                  # Linux/Mac合并: bash rejoin.sh
rejoin.bat                                 # Windows双击合并
scripts/                                   # Agnes全管线脚本(图像+视频+编排+剪辑+字幕)
```

## 还原完整交付包

- Windows: 双击 `rejoin.bat`
- Linux/Mac: `bash rejoin.sh`

得到 `749收容漫剧_最终交付.zip`，解压后：

```
00_剧集设定_改编评估与剧集圣经.md
01_剧本_第1-2集.md            # 两集完整剧本(18+19镜)
02_角色资产/ + 02_角色资产卡.md  # 5角色身份证+定妆照
03_场景资产/ + 03_场景资产卡.md  # 6场景DNA卡
04_分镜成图/                    # 76张分镜图(1080x1920)
04_分镜表_第1-2集.md
05_出图提示词_第1-2集.md        # 37条模板C出图提示词
05_动态片段/                    # 37条视频片段(含同期声对白)
06_动态提示词_第1-2集.md        # 37条模板D动态提示词(对白内嵌)
06_音频/                       # SRT字幕
07_成片/                       # ★ 两集成片: 第1集《血石》94.4s / 第2集《罔象》100.6s (1080x1920/30fps/AAC)
07_音频方案与音效表.md
_任务状态/                      # Agnes任务断点状态(imgurls+任务json)
```

## 管线脚本用法(scripts/)

1. `agnes_img.mjs` — 图像生成: `node agnes_img.mjs "提示词|输出.png|1080x1920"`(模型 agnes-image-2.5-flash)
2. `drama749_lane.mjs` — 双车道视频编排(核心):
   - `node drama749_lane.mjs once|loop|status|qa <镜号>`
   - 车道A agnes-video-2.5-flash: `mode=keyframe`(单数) + first_frame → 原生竖屏704x1280
   - 车道B agnes-video-v2.0: `mode=ti2vid` + image数组 → 内容跟分镜, 固定1088x832横屏, 剪辑时模糊垫底转竖屏
   - 注意: agnes-video-2.5 是付费模型(403配额$0)不可用; v2.0 合法mode=ti2vid/keyframes/multi_reference
   - 免费档限流: 429/503 快速让出+30s循环重试; 提交间隔管理在 cycle() 内
3. `drama749_cut.sh [集数]` — 剪辑: 横屏片段自动模糊垫底→归一化1080x1920/30fps→拼接→SRT字幕+AI角标烧录→片尾悬念卡
4. `drama749_srt.py` — 按片段实际时长平铺台词生成SRT
5. 素材数据: `drama749_assets.mjs`(角色/场景) + `drama749_ep1.mjs`/`drama749_ep2.mjs`(37镜出图+动态提示词) + `drama749_shots_subs.json`(字幕)

## 密钥(不入库)

- Agnes: `scripts/agnes_key.txt`(本地保留) — 图像/视频 API key
- GitHub: `gh_token.txt`(本地保留) — 绝不推送

## 版本记录

- 2026-10-04: 重建本分支。删除 sanguo-drama 分支(旧三国短剧,质量不达标,释放约280MB), 本分支改为孤儿历史全量承载新交付包与管线脚本。
