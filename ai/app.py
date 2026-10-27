import os
import threading
import time
from datetime import datetime

import cv2
from flask import Flask, Response, jsonify, render_template

from detector import PlantDiseaseDetector


# =========================
# Configuration
# =========================
# 所有地址/开关都可以用环境变量覆盖，方便换机器、换网络后直接运行：
#   CAMERA_URL=http://192.168.1.50:81/stream
#   USE_DEMO_IMAGE=0          关闭演示图片模式，改为拉取真实 ESP32-CAM 视频流
#   ENABLE_OPENCV_WINDOW=0    服务器/无桌面环境下关闭 OpenCV 本地预览窗口
CAMERA_URL = os.environ.get("CAMERA_URL", "http://192.168.1.50:81/stream")
ENABLE_OPENCV_WINDOW = os.environ.get("ENABLE_OPENCV_WINDOW", "1").lower() not in ("0", "false", "no")
DETECTION_INTERVAL_SECONDS = 1.0
RECONNECT_DELAY_SECONDS = 1.0
USE_DEMO_IMAGE = os.environ.get("USE_DEMO_IMAGE", "1").lower() not in ("0", "false", "no")
DEMO_IMAGE_FILENAME = "demo_live.jpg"


BASE_DIR = os.path.dirname(os.path.abspath(__file__))
STATIC_DIR = os.path.join(BASE_DIR, "static")
LATEST_CAPTURE_PATH = os.path.join(STATIC_DIR, "latest_capture.jpg")
DEMO_IMAGE_PATH = os.path.join(STATIC_DIR, DEMO_IMAGE_FILENAME)

app = Flask(__name__)


class CameraBrain:
    def __init__(self, camera_url: str):
        self.camera_url = camera_url
        self.detector = PlantDiseaseDetector()
        self.lock = threading.Lock()
        self.running = False
        self.frame = None
        self.last_boxes = []
        self.last_detection_time = 0.0
        self.result_info = {
            "class_name": "healthy",
            "confidence": 0.0,
            "status": "系统启动中",
            "suggestion": "正在连接 ESP32-CAM，请确认电脑可以访问视频流地址。",
            "raw_class": "",
            "boxes": [],
            "last_capture_time": "-",
            "camera_url": self.camera_url,
            "camera_connected": False,
            "box_count": 0,
        }

    def start(self):
        if self.running:
            return
        self.running = True
        target = self._demo_image_loop if USE_DEMO_IMAGE else self._capture_loop
        threading.Thread(target=target, daemon=True).start()

    def get_frame_copy(self):
        with self.lock:
            if self.frame is None:
                return None
            return self.frame.copy()

    def get_result_info(self):
        with self.lock:
            return dict(self.result_info)

    def _update_result(self, **kwargs):
        with self.lock:
            self.result_info.update(kwargs)

    def _demo_image_loop(self):
        os.makedirs(STATIC_DIR, exist_ok=True)

        while self.running:
            raw_frame = cv2.imread(DEMO_IMAGE_PATH)
            if raw_frame is None:
                self._update_result(
                    camera_connected=False,
                    status="示例图片读取失败",
                    suggestion=f"请检查示例图片是否存在：{DEMO_IMAGE_PATH}",
                )
                time.sleep(RECONNECT_DELAY_SECONDS)
                continue

            now = time.time()
            if now - self.last_detection_time >= DETECTION_INTERVAL_SECONDS:
                self.last_detection_time = now
                annotated_frame, result_info = self.detector.detect(raw_frame)
                self._save_detection(annotated_frame, result_info)
            else:
                annotated_frame = self.detector.draw_boxes(raw_frame.copy(), self.last_boxes)

            with self.lock:
                self.frame = annotated_frame

            if ENABLE_OPENCV_WINDOW:
                cv2.imshow("ESP32-CAM YOLO Plant Disease Detection", annotated_frame)
                if cv2.waitKey(1) & 0xFF == ord("q"):
                    cv2.destroyWindow("ESP32-CAM YOLO Plant Disease Detection")

            time.sleep(0.03)

    def _capture_loop(self):
        os.makedirs(STATIC_DIR, exist_ok=True)

        while self.running:
            cap = cv2.VideoCapture(self.camera_url)

            if not cap.isOpened():
                self._update_result(
                    camera_connected=False,
                    status="摄像头连接失败",
                    suggestion=f"请检查 ESP32-CAM 地址是否正确：{self.camera_url}",
                )
                cap.release()
                time.sleep(RECONNECT_DELAY_SECONDS)
                continue

            self._update_result(
                camera_connected=True,
                status="视频流连接正常",
                suggestion="请将叶片放在画面中央，保持光照稳定，等待 YOLO 识别结果。",
            )

            while self.running and cap.isOpened():
                ok, raw_frame = cap.read()
                if not ok or raw_frame is None:
                    self._update_result(
                        camera_connected=False,
                        status="视频流中断",
                        suggestion="正在尝试重连。若频繁中断，请检查 ESP32-CAM 供电、Wi-Fi 和视频流地址。",
                    )
                    break

                now = time.time()
                if now - self.last_detection_time >= DETECTION_INTERVAL_SECONDS:
                    self.last_detection_time = now
                    annotated_frame, result_info = self.detector.detect(raw_frame)
                    self._save_detection(annotated_frame, result_info)
                else:
                    annotated_frame = self.detector.draw_boxes(raw_frame.copy(), self.last_boxes)

                with self.lock:
                    self.frame = annotated_frame

                if ENABLE_OPENCV_WINDOW:
                    cv2.imshow("ESP32-CAM YOLO Plant Disease Detection", annotated_frame)
                    if cv2.waitKey(1) & 0xFF == ord("q"):
                        cv2.destroyWindow("ESP32-CAM YOLO Plant Disease Detection")

            cap.release()
            time.sleep(RECONNECT_DELAY_SECONDS)

    def _save_detection(self, annotated_frame, result_info):
        boxes = result_info.get("boxes", [])
        capture_time = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        cv2.imwrite(LATEST_CAPTURE_PATH, annotated_frame)

        with self.lock:
            self.last_boxes = boxes
            self.result_info.update(result_info)
            self.result_info.update(
                {
                    "last_capture_time": capture_time,
                    "camera_connected": True,
                    "box_count": len(boxes),
                    "camera_url": self.camera_url,
                }
            )


brain = CameraBrain(CAMERA_URL)


def generate_mjpeg():
    while True:
        frame = brain.get_frame_copy()
        if frame is None:
            time.sleep(0.05)
            continue

        ok, buffer = cv2.imencode(".jpg", frame)
        if not ok:
            continue

        yield b"--frame\r\nContent-Type: image/jpeg\r\n\r\n" + buffer.tobytes() + b"\r\n"


@app.route("/")
def index():
    return render_template("index.html", camera_url=CAMERA_URL)


@app.route("/video_feed")
def video_feed():
    return Response(generate_mjpeg(), mimetype="multipart/x-mixed-replace; boundary=frame")


@app.route("/result")
def result():
    return jsonify(brain.get_result_info())


@app.route("/api/status")
def api_status():
    data = brain.get_result_info()
    data["latest_capture_url"] = "/static/latest_capture.jpg"
    return jsonify(data)


if __name__ == "__main__":
    brain.start()
    app.run(host="0.0.0.0", port=5000, debug=False, threaded=True)
