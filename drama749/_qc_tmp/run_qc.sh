#!/bin/bash
# 用法: run_qc.sh <任务文件>  每行: tag<TAB>图片名<TAB>提示词
cd /home/z/my-project/749收容怪物_漫剧/_gen_images
while IFS=$'\t' read -r tag img prompt; do
  [ -z "$tag" ] && continue
  out="/home/z/my-project/749收容怪物_漫剧/_qc_tmp/qc_${tag}.json"
  if [ -s "$out" ]; then echo "SKIP $tag (已有结果)"; continue; fi
  ok=0
  for attempt in 1 2 3 4; do
    if z-ai vision -p "$prompt" -i "$img" -o "$out" >/dev/null 2>&1; then
      echo "DONE $tag"
      ok=1
      break
    else
      echo "RETRY $tag attempt=$attempt"
      sleep 50
    fi
  done
  [ $ok -eq 0 ] && echo "FAILED $tag"
  sleep 8
done < "$1"
echo "ALL FINISHED"
