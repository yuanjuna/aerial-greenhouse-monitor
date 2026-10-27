import os
from typing import Dict, List, Tuple

import cv2
import numpy as np
import torch
from ultralytics import YOLO


BASE_DIR = os.path.dirname(os.path.abspath(__file__))

# 模型放在 ai/model/best.pt；也可以用 YOLO_MODEL_PATH 指向别处的权重。
MODEL_PATH = os.environ.get("YOLO_MODEL_PATH") or os.path.join(BASE_DIR, "model", "best.pt")
CONF_THRESHOLD = 0.25
FALLBACK_CONF_THRESHOLD = 0.12
IMG_SIZE = 640
CLASS_CONFIRM_THRESHOLDS = {
    "pest": 0.30,
    "yellow_leaf": 0.35,
    "disease_spot": 0.45,
    "healthy": 0.25,
    "unknown": 0.60,
}


class PlantDiseaseDetector:
    """
    Local YOLO detector.

    detect(frame) returns:
    - annotated_frame
    - result_info

    class_name is one of:
    pest / disease_spot / yellow_leaf / healthy / unknown
    """

    def __init__(self, model_path: str = MODEL_PATH):
        self.model_path = model_path
        self._ensure_model_path()
        self.device = "cuda" if torch.cuda.is_available() else "cpu"
        self.model = YOLO(model_path)

        print("[YOLO] model path:", model_path)
        print("[YOLO] device:", self.device)
        print("[YOLO] imgsz:", IMG_SIZE)
        print("[YOLO] conf threshold:", CONF_THRESHOLD)
        print("[YOLO] fallback conf threshold:", FALLBACK_CONF_THRESHOLD)
        print("[YOLO] model.names:", self.model.names)

        self.keyword_map = {
            "pest": [
                "pest",
                "bug",
                "beetle",
                "larva",
                "larvae",
                "worm",
                "aphid",
                "mite",
                "miner",
                "thrips",
                "whitefly",
                "mealy",
                "milli",
                "insect",
                "spider",
            ],
            "yellow_leaf": [
                "yellow",
                "curl",
                "wilt",
                "wilted",
                "dried",
                "dry",
                "chlorosis",
                "necrosis",
                "leaf curl",
                "yellowing",
            ],
            "disease_spot": [
                "spot",
                "blight",
                "rust",
                "mildew",
                "mold",
                "mould",
                "rot",
                "scab",
                "bacterial",
                "fungi",
                "fungal",
                "disease",
                "anthracnose",
                "septoria",
                "cercospora",
                "powdery",
                "smut",
                "canker",
                "blast",
            ],
            "healthy": [
                "healthy",
                "normal",
                "good",
            ],
        }

    def detect(self, frame) -> Tuple[object, Dict[str, object]]:
        results = self.model(
            frame,
            conf=CONF_THRESHOLD,
            imgsz=IMG_SIZE,
            device=self.device,
            verbose=False,
        )
        boxes = self._extract_boxes(results)

        if not boxes:
            results = self.model(
                frame,
                conf=FALLBACK_CONF_THRESHOLD,
                imgsz=IMG_SIZE,
                device=self.device,
                verbose=False,
            )
            boxes = self._extract_boxes(results, min_confidence=FALLBACK_CONF_THRESHOLD)

        boxes = self._filter_boxes_by_class_confidence(boxes)
        if not boxes:
            boxes = self._detect_visual_leaf_spots(frame)
        annotated_frame = self.draw_boxes(frame.copy(), boxes)
        result_info = self._build_result_info(boxes)
        return annotated_frame, result_info

    def draw_boxes(self, frame, boxes: List[Dict[str, object]]):
        for item in boxes:
            x1, y1, x2, y2 = item["xyxy"]
            class_name = item["class_name"]
            confidence = item["confidence"]

            color = (40, 180, 80) if class_name == "healthy" else (30, 90, 230)
            label = f"{class_name} {confidence:.2f}"

            cv2.rectangle(frame, (x1, y1), (x2, y2), color, 2)
            text_y = max(y1 - 8, 18)
            cv2.putText(
                frame,
                label,
                (x1, text_y),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.55,
                color,
                2,
                cv2.LINE_AA,
            )

        return frame

    def _extract_boxes(self, results, min_confidence: float = CONF_THRESHOLD) -> List[Dict[str, object]]:
        boxes = []
        for result in results:
            for box in result.boxes:
                confidence = float(box.conf[0])
                if confidence < min_confidence:
                    continue

                class_id = int(box.cls[0])
                raw_class = str(result.names[class_id])
                class_name = self._map_raw_class(raw_class)
                x1, y1, x2, y2 = [int(value) for value in box.xyxy[0].tolist()]

                boxes.append(
                    {
                        "xyxy": [x1, y1, x2, y2],
                        "raw_class": raw_class,
                        "class_name": class_name,
                        "confidence": confidence,
                    }
                )

        return boxes

    def _filter_boxes_by_class_confidence(self, boxes: List[Dict[str, object]]) -> List[Dict[str, object]]:
        filtered = []
        for item in boxes:
            class_name = item["class_name"]
            threshold = CLASS_CONFIRM_THRESHOLDS.get(class_name, 0.60)
            if item["confidence"] >= threshold:
                filtered.append(item)
        return filtered

    def _detect_visual_leaf_spots(self, frame) -> List[Dict[str, object]]:
        hsv = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)

        green_mask = cv2.inRange(hsv, np.array([25, 25, 35]), np.array([105, 255, 235]))
        green_mask = cv2.morphologyEx(green_mask, cv2.MORPH_OPEN, np.ones((5, 5), np.uint8))
        green_mask = cv2.morphologyEx(green_mask, cv2.MORPH_CLOSE, np.ones((9, 9), np.uint8))

        leaf_area = int(cv2.countNonZero(green_mask))
        if leaf_area < 1500:
            return []

        dark_mask = cv2.inRange(hsv, np.array([0, 0, 0]), np.array([179, 140, 115]))
        low_saturation_spot_mask = cv2.inRange(hsv, np.array([0, 0, 35]), np.array([179, 95, 155]))
        spot_mask = cv2.bitwise_or(dark_mask, low_saturation_spot_mask)
        spot_mask = cv2.bitwise_and(spot_mask, green_mask)
        spot_mask = cv2.morphologyEx(spot_mask, cv2.MORPH_OPEN, np.ones((3, 3), np.uint8))

        contours, _ = cv2.findContours(spot_mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        spot_boxes = []
        spot_area_total = 0

        for contour in contours:
            area = cv2.contourArea(contour)
            if area < 30:
                continue

            x, y, w, h = cv2.boundingRect(contour)
            if w < 4 or h < 4:
                continue

            spot_area_total += area
            spot_boxes.append([x, y, x + w, y + h])

        spot_ratio = spot_area_total / max(leaf_area, 1)
        if len(spot_boxes) < 3 and spot_ratio < 0.015:
            return []

        x1 = min(box[0] for box in spot_boxes)
        y1 = min(box[1] for box in spot_boxes)
        x2 = max(box[2] for box in spot_boxes)
        y2 = max(box[3] for box in spot_boxes)
        confidence = min(0.80, max(0.46, spot_ratio * 8))

        return [
            {
                "xyxy": [x1, y1, x2, y2],
                "raw_class": "opencv_visual_leaf_spots",
                "class_name": "disease_spot",
                "confidence": float(confidence),
            }
        ]

    def _build_result_info(self, boxes: List[Dict[str, object]]) -> Dict[str, object]:
        if not boxes:
            return {
                "class_name": "healthy",
                "confidence": 0.0,
                "status": "未检测到明显异常",
                "suggestion": "当前画面未发现明显虫害、病斑或枯黄区域，请确保叶片位于画面中央。",
                "raw_class": "",
                "boxes": [],
            }

        best_box = max(boxes, key=lambda item: item["confidence"])
        class_name = best_box["class_name"]

        return {
            "class_name": class_name,
            "confidence": round(float(best_box["confidence"]), 2),
            "status": self._status_for_class(class_name),
            "suggestion": self._suggestion_for_class(class_name),
            "raw_class": best_box["raw_class"],
            "boxes": boxes,
        }

    def _map_raw_class(self, raw_class: str) -> str:
        normalized = raw_class.lower().replace("-", " ").replace("_", " ")

        # Priority: pest > yellow_leaf > disease_spot > healthy > unknown.
        for class_name in ("pest", "yellow_leaf", "disease_spot", "healthy"):
            if any(keyword in normalized for keyword in self.keyword_map[class_name]):
                return class_name

        # Models often output plain leaf classes like "cherry leaf".
        # If no abnormal keyword is found, treat plain leaf as healthy.
        if "leaf" in normalized:
            return "healthy"

        return "unknown"

    def _status_for_class(self, class_name: str) -> str:
        if class_name == "pest":
            return "检测到疑似虫害"
        if class_name == "disease_spot":
            return "检测到疑似病斑"
        if class_name == "yellow_leaf":
            return "检测到疑似枯黄异常"
        if class_name == "healthy":
            return "叶片状态正常"
        return "检测到未知植物异常"

    def _suggestion_for_class(self, class_name: str) -> str:
        if class_name == "pest":
            return "建议检查叶片背面、嫩芽和茎部，确认是否存在害虫或虫卵。"
        if class_name == "disease_spot":
            return "建议人工复查病斑区域，观察是否存在扩散、霉层或坏死斑。"
        if class_name == "yellow_leaf":
            return "建议检查水分、光照、养分和根系状态，必要时调整灌溉、补光或施肥。"
        if class_name == "healthy":
            return "当前未发现明显病虫害或枯黄异常。"
        return "模型检测到异常目标，但类别无法稳定映射，建议人工复查。"

    def _ensure_model_path(self):
        model_dir = os.path.dirname(self.model_path)
        os.makedirs(model_dir, exist_ok=True)

        if os.path.exists(self.model_path):
            return

        download_url = "https://huggingface.co/JK-TK/PlantDiseaseDetection/resolve/main/PlantDiseaseDetection.pt"
        raise FileNotFoundError(
            f"YOLO model not found: {self.model_path}\n"
            f"Please download it manually from:\n{download_url}\n"
            f"Then save it as:\n{self.model_path}"
        )
