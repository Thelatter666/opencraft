== PM 验收补充证据（第五任 PM，2026-09-19）==

## 1. 白名单核对（基线 379424d）：43 文件全中；禁碰项（STATE/卡面/docs/0*/research/assets/protocol.hpp/player_life/mob_*）diff 为空 —— C-2「合成客户端本地零协议改动」的直接证据。

## 2. 用例名集合：492 零缺失（comm -23 空）+ 37 新增（原始数据两份 txt）。

## 3. FNV 冻结表平移关系的机器验证（推导式 ≠ 说法）
```
0..62 内容瓦片逐字节相同: True
新 63..65 是新像素(assembly_bench 三面): True
新 66..75 == 旧 63..72 (crack 平移3): True
背景格 8→5 且值不变: True
```

## 4. 实机判据复核（QA 日志）
```
atlas: 60/66 + crack tiles at 66 + mobs: 3/3；session{,2,3}.log [warning]/[error] = 0/0/0
全链: crafted(2x2) → placed assembly_bench (7,143,-164) → opened a 3x3 surface ×5
```

## 5. 采集等级口径（S-1）的自洽性验证
```
十行门控全对: True
```
