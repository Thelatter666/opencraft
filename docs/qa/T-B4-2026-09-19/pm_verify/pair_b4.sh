#!/bin/bash
# T-B4 PM 侧 A/B 配对（照 T-B5 pair.sh 写，路径改成本卡）
# 拍一对（模型 / 占位），用无生物区（左 300px）检验相机是否同位；只有 ratio=1.0 且 dx=dy=0 才接受
set -u
R=/Users/happy/Desktop/opencraft_scratch/verify-tb4
P=/Users/happy/Desktop/opencraft_scratch/tb4-shots
T=/Users/happy/Desktop/opencraft_scratch/ti2input
SCENE=/Users/happy/Desktop/opencraft/docs/qa/T-B3-2026-09-18/pm_verify/run_scene_pre.sh
TAG=bb
for i in 1 2 3 4 5 6; do
  rm -rf $P/${TAG}_m_$i $P/${TAG}_s_$i
  bash $SCENE $R $P/${TAG}_m_$i blastbud $T >/dev/null 2>&1
  mv $R/assets/mobs/blastbud.vox $R/off.bin
  bash $SCENE $R $P/${TAG}_s_$i blastbud $T >/dev/null 2>&1
  mv $R/off.bin $R/assets/mobs/blastbud.vox
  if [ ! -f $P/${TAG}_m_$i/shot_a.png ] || [ ! -f $P/${TAG}_s_$i/shot_a.png ]; then echo "attempt $i: missing shot"; continue; fi
  sm=$(grep -c 'EVIDENCE summon' $P/${TAG}_m_$i/session.log)
  ss=$(grep -c 'EVIDENCE summon' $P/${TAG}_s_$i/session.log)
  R2=$(python3 - $P/${TAG}_m_$i/shot_a.png $P/${TAG}_s_$i/shot_a.png <<'PY'
import sys, random
from PIL import Image
a=Image.open(sys.argv[1]).convert('RGB').load(); b=Image.open(sys.argv[2]).convert('RGB').load()
random.seed(7); pts=[(random.randrange(40,300), random.randrange(200,700)) for _ in range(2500)]
best=(-1,9,9)
for dx in range(-2,3):
    for dy in range(-2,3):
        same=sum(1 for x,y in pts if a[x,y]==b[x+dx,y+dy])
        if same>best[0]: best=(same,dx,dy)
print(f"{best[0]/len(pts):.4f} {best[1]} {best[2]}")
PY
)
  echo "attempt $i: summon(model)=$sm summon(standin)=$ss  align ratio/dx/dy = $R2"
  set -- $R2
  if [ "$sm" = "1" ] && [ "$ss" = "1" ] && [ "$1" = "1.0000" ] && [ "$2" = "0" ] && [ "$3" = "0" ]; then echo "ACCEPT: ${TAG}_m_$i (模型) vs ${TAG}_s_$i (占位)"; break; fi
done
