# 🚀 Linux Camera Media Pipeline

> A multi-threaded media processing pipeline based on **V4L2 + SDL2 + FFmpeg**
> Supports **real-time preview / RTSP streaming / MP4 recording**

---

## 📌 Overview

This project implements a complete Linux camera media pipeline:

* 📷 Capture video using **V4L2**
* 🖥 Display frames in real-time using **SDL2**
* 🎥 Encode video using **FFmpeg (libavcodec)**
* 🌐 Stream via **RTSP**
* 💾 Record to **MP4**

### ✨ Key Focus

> ❗ Not just calling APIs — but building a **full media pipeline architecture**

---

## 🧠 Architecture

### 🧩 Module Design

```text
app        → control layer (input + state)
device     → V4L2 capture
media      → frame abstraction + queue
pipeline   → encoding + streaming + recording
ui         → display
```

---

## 🔄 Data Flow

```text
V4L2 (YUYV)
   ↓
capture thread
   ↓
FrameQueue
   ↓
 ┌──────────────┬──────────────┐
 ↓              ↓
stream thread   record thread
 ↓              ↓
RTSP            MP4
```

---

## 🧵 Thread Model

| Thread         | Responsibility           |
| -------------- | ------------------------ |
| capture thread | Capture frames from V4L2 |
| stream thread  | Encode + RTSP streaming  |
| record thread  | Encode + MP4 recording   |
| main thread    | UI + control             |

---

## ⚙️ Key Features

### 1️⃣ V4L2 Deep Integration

* mmap buffer management
* `bytesperline / sizeimage` handling
* `v4l2_buffer.timestamp`
* frame sequence tracking (drop detection)

---

### 2️⃣ Frame Abstraction

```c
typedef struct FramePacket {
    uint8_t *data;
    int width;
    int height;
    int stride;
    uint32_t pixfmt;
    uint64_t frame_id;
    CaptureMeta meta;
} FramePacket;
```

---

### 3️⃣ Multi-threaded Pipeline

* Producer: capture thread
* Consumers: stream / record threads
* Lock-based queue
* Drop strategy: overwrite oldest frame

---

### 4️⃣ Timestamp Handling (Important)

```text
V4L2 timestamp → microseconds → PTS → FFmpeg time_base
```

✔ Avoids naive:

```text
frame_index++
```

---

### 5️⃣ Pixel Format Conversion

```text
YUYV → YUV420P → H264
```

Using:

```c
sws_scale()
```

---

### 6️⃣ Engineering Design

* Modular architecture
* Lifecycle management
* Fault isolation (fatal_error)
* Config system
* Logging system
* Runtime statistics

---

## 🏗 Project Structure

```text
linux_camera_pipeline/
│
├── app/        # control + config + startup
├── device/     # V4L2
├── media/      # frame + queue
├── pipeline/   # stream / record
├── ui/         # SDL display
├── utils/      # log
│
├── main.c
├── Makefile
└── README.md
```

---

## 🚀 Build & Run

### 🔧 Dependencies

* FFmpeg (libavcodec, libavformat, libswscale)
* SDL2
* Linux V4L2

Install (Ubuntu):

```bash
sudo apt install libsdl2-dev libavcodec-dev libavformat-dev libswscale-dev
```

---

### 🔨 Build

```bash
make
```

---

### ▶️ Run

```bash
./app
```

---

## ⌨️ Controls

| Key | Action              |
| --- | ------------------- |
| q   | Quit                |
| p   | Pause               |
| t   | Toggle RTSP         |
| r   | Toggle Recording    |
| s   | Save Snapshot       |
| ↑ ↓ | Select control      |
| ← → | Adjust control      |
| i   | Print runtime state |
| h   | Help                |

---

## 📊 Runtime Output Example

```text
[INFO] capture=120 fps
[INFO] stream_encoded=118
[INFO] record_encoded=120
[INFO] dropped=2
```

---

## 🔍 Highlights

### ✔ Frame Queue Design

* decouples capture & encoding
* avoids blocking
* supports frame dropping

---

### ✔ Accurate Timing

* real timestamps from V4L2
* mapped to encoder time_base

---

### ✔ Robust Pipeline

* independent threads
* failure isolation
* controlled shutdown & flush

---

## 📈 Future Improvements

* WebRTC streaming
* GStreamer backend
* Hardware encoding (NVENC / V4L2 M2M)
* Multi-camera support
* Qt GUI

---

## 🎯 What This Project Demonstrates

* Linux multimedia programming
* V4L2 internals understanding
* FFmpeg encoding pipeline
* Multi-thread system design
* Real-time data flow architecture

---

## 🧑‍💻 Author Notes

This project is designed not only to "work", but to:

> **Demonstrate deep understanding of camera pipelines and media systems**

---

## ⭐ If You Like This Project

Give it a star ⭐ and feel free to fork!

---
