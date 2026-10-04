#!/bin/bash
# 《749收容》剪辑(重建版)：归一化/横屏模糊垫底→拼接→字幕+AI角标→片尾悬念卡
set -e
OUT="/home/z/my-project/download/749收容漫剧"
CLIPS="$OUT/05_动态片段"
TMP="/home/z/my-project/scripts/drama749_tmp"
FONTDIR="/home/z/my-project/scripts/fonts"
FONT="$FONTDIR/NotoSansSC.ttf"
mkdir -p "$TMP" "$OUT/06_音频" "$OUT/07_成片"
if [ ! -f "$FONT" ]; then
  mkdir -p "$FONTDIR"
  cp /usr/share/fonts/truetype/chinese/NotoSansSC-Regular.ttf "$FONT" 2>/dev/null || cp "$(fc-match -f '%{file}' 'Noto Sans SC')" "$FONT"
fi

python3 /home/z/my-project/scripts/drama749_srt.py

ONLY_EP="${1:-}"
for EP in 1 2; do
  if [ -n "$ONLY_EP" ] && [ "$EP" != "$ONLY_EP" ]; then continue; fi
  echo "=== 第${EP}集 ==="
  MAP="$TMP/ep${EP}_list.txt"; : > "$MAP"
  for f in "$CLIPS"/第${EP}集_镜*.mp4; do
    [ -f "$f" ] || continue
    b=$(basename "$f" .mp4)
    n="$TMP/${b}_n.mp4"
    W=$(ffprobe -v error -select_streams v:0 -show_entries stream=width -of csv=p=0 "$f")
    H=$(ffprobe -v error -select_streams v:0 -show_entries stream=height -of csv=p=0 "$f")
    if [ "$W" -gt "$H" ]; then
      echo "  $b 横屏${W}x${H} → 模糊垫底"
      ffmpeg -y -v error -i "$f" -filter_complex "[0:v]scale=1080:1920:force_original_aspect_ratio=increase,crop=1080:1920,boxblur=24:2,eq=brightness=-0.08[bg];[0:v]scale=1080:-2[fg];[bg][fg]overlay=(W-w)/2:(H-h)/2,setsar=1,fps=30" \
        -c:v libx264 -preset veryfast -crf 20 -pix_fmt yuv420p -c:a aac -ar 44100 -ac 2 -vsync cfr "$n"
    else
      ffmpeg -y -v error -i "$f" -vf "scale=1080:1920,setsar=1,fps=30" \
        -c:v libx264 -preset veryfast -crf 20 -pix_fmt yuv420p -c:a aac -ar 44100 -ac 2 -vsync cfr "$n"
    fi
    echo "file '$n'" >> "$MAP"
  done
  N=$(wc -l < "$MAP"); echo "clips: $N"
  ffmpeg -y -v error -f concat -safe 0 -i "$MAP" \
    -vf "subtitles=$OUT/06_音频/ep${EP}.srt:fontsdir=$FONTDIR:force_style='FontName=Noto Sans SC,FontSize=10,Outline=1.2,Shadow=0,MarginV=26',drawtext=fontfile=$FONT:text='AI生成':fontsize=26:fontcolor=white@0.65:x=w-tw-28:y=28:box=1:boxcolor=black@0.25:boxborderw=8" \
    -c:v libx264 -preset medium -crf 20 -pix_fmt yuv420p -c:a aac -ar 44100 -ac 2 \
    "$TMP/ep${EP}_sub.mp4"
  if [ "$EP" = "1" ]; then
    T1="他们盯上的人"; T2="还不知道自己被盯上了。"; T3="第1集完 · 下集：罔象夜袭"; NAME="749收容_第1集_血石_成片.mp4"
  else
    T1="749的档案里，多了一个"; T2="S级收容人编号。"; T3="第2集完"; NAME="749收容_第2集_罔象_成片.mp4"
  fi
  ffmpeg -y -v error -f lavfi -i "color=c=black:s=1080x1920:d=4.5:r=30" -f lavfi -i "anullsrc=r=44100:cl=stereo:d=4.5" \
    -vf "drawtext=fontfile=$FONT:text='${T1}':fontsize=64:fontcolor=white:x=(w-tw)/2:y=h/2-120,drawtext=fontfile=$FONT:text='${T2}':fontsize=64:fontcolor=white:x=(w-tw)/2:y=h/2-30,drawtext=fontfile=$FONT:text='${T3}':fontsize=36:fontcolor=white@0.7:x=(w-tw)/2:y=h/2+90" \
    -shortest -c:v libx264 -preset veryfast -crf 20 -pix_fmt yuv420p -c:a aac "$TMP/ep${EP}_end.mp4"
  printf "file '%s/ep${EP}_sub.mp4'\nfile '%s/ep${EP}_end.mp4'\n" "$TMP" "$TMP" > "$TMP/ep${EP}_final.txt"
  ffmpeg -y -v error -f concat -safe 0 -i "$TMP/ep${EP}_final.txt" -c copy "$OUT/07_成片/$NAME"
  echo "第${EP}集成片OK: $NAME"
done
echo "ALL DONE"
