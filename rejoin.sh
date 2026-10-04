#!/bin/bash
# 合并分卷还原完整zip: bash rejoin.sh
cat 成品分卷/749收容漫剧_最终交付.zip.part-* > 749收容漫剧_最终交付.zip && echo "OK: $(du -h 749收容漫剧_最终交付.zip)" && unzip -t 749收容漫剧_最终交付.zip > /dev/null && echo "完整性校验通过"
