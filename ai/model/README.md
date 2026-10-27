# 模型权重目录

`best.pt`（YOLO 检测权重，约 20 MB ~ 460 MB）体积较大，**不随仓库提交**，需要自行放置到本目录：

```
ai/model/best.pt
```

## 获取方式

1. **使用公开权重**：`detector.py` 里已内置下载地址
   <https://huggingface.co/JK-TK/PlantDiseaseDetection>

   下载后重命名为 `best.pt` 放到本目录即可。

2. **使用自己训练的权重**：用 Ultralytics YOLO 训练后，把 `runs/detect/train/weights/best.pt` 拷到本目录。

3. **放到别的位置**：设置环境变量指向它，不必改代码

   ```powershell
   $env:YOLO_MODEL_PATH = "D:\models\my_best.pt"
   ```

## 类别说明

模型原始类别名不固定，`detector.py` 会用关键词把它映射为 5 个通用类别：

| 通用类别 | 含义 | 触发关键词（节选） |
| --- | --- | --- |
| `pest` | 虫害 | pest / bug / aphid / mite / worm ... |
| `disease_spot` | 病斑 | spot / blight / rust / mildew / rot ... |
| `yellow_leaf` | 枯黄 | yellow / wilt / chlorosis / necrosis ... |
| `healthy` | 健康 | healthy / normal，以及未命中异常关键词的普通叶片类 |
| `unknown` | 未知异常 | 其余类别 |

因此换成别的植物病虫害模型通常也能直接用，无需改前端。
