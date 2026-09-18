#!/bin/bash
# 拍一对（模型/占位）并用无生物区检验相机是否同位；同位才接受
R=/Users/happy/Desktop/opencraft_scratch/tb5-evid; P=/Users/happy/Desktop/opencraft_scratch/tb5-shots; T=/Users/happy/Desktop/opencraft_scratch/ti2input
MODE=$1; MODELFILE=$2; TAG=$3
for i in 1 2 3 4 5; do
  bash $P/run_scene_pre.sh $R $P/${TAG}_m_$i $MODE $T >/dev/null 2>&1
  mv $R/assets/mobs/$MODELFILE $R/off.bin
  bash $P/run_scene_pre.sh $R $P/${TAG}_s_$i $MODE $T >/dev/null 2>&1
  mv $R/off.bin $R/assets/mobs/$MODELFILE
  if [ ! -f $P/${TAG}_m_$i/shot_a.png ] || [ ! -f $P/${TAG}_s_$i/shot_a.png ]; then echo "attempt $i: missing shot"; continue; fi
  R2=$(python3 - $P/${TAG}_m_$i/shot_a.png $P/${TAG}_s_$i/shot_a.png <<'PY'
import sys, random
from PIL import Image
a=Image.open(sys.argv[1]).convert('RGB').load(); b=Image.open(sys.argv[2]).convert('RGB').load()
random.seed(7); pts=[(random.randrange(40,300), random.randrange(200,700)) for _ in range(2500)]
best=(-1,9,9)
for dx in range(-2,3):
    for dy in range(-2,3):
        same=sum(1 for x,y in pts if a[x,y]==b[x+dx,y+dy]); 
        if same>best[0]: best=(same,dx,dy)
print(f"{best[0]/len(pts):.4f} {best[1]} {best[2]}")
PY
)
  echo "attempt $i: left-region align ratio/dx/dy = $R2"
  set -- $R2
  if [ "$1" = "1.0000" ] && [ "$2" = "0" ] && [ "$3" = "0" ]; then echo "ACCEPT ${TAG}_m_$i vs ${TAG}_s_$i"; break; fi
done
