@echo off
REM Windows下双击运行,合并分卷还原完整zip
copy /b "成品分卷\749收容漫剧_最终交付.zip.part-00" + "成品分卷\749收容漫剧_最终交付.zip.part-01" + "成品分卷\749收容漫剧_最终交付.zip.part-02" "749收容漫剧_最终交付.zip"
echo 合并完成: 749收容漫剧_最终交付.zip
pause
