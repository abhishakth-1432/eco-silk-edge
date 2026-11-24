
# 🐛 Eco-Silk Edge

### A TinyML-Powered Autonomous System for Disease Detection and Precision Environmental Control in Sericulture

## 📌 About the Project

Eco-Silk Edge is an intelligent sericulture monitoring system designed to detect silkworm diseases using TinyML and Computer Vision while monitoring environmental conditions.

The system uses an ESP32-S3 camera module and a machine learning model to classify silkworm health conditions.

## 🎯 Objectives

- Detect silkworm diseases automatically.
- Classify silkworm images into Healthy, Pebrine and Grasserie.
- Monitor temperature and humidity.
- Automate environmental control using a fan and simulated heater.
- Provide a real-time monitoring dashboard.

## 🛠️ Technologies Used

- ESP32-S3
- OV2640 Camera
- DHT22 Temperature and Humidity Sensor
- Python
- TensorFlow / LiteRT
- MobileNetV2
- OpenCV
- Arduino IDE
- HTML, CSS and JavaScript

## 🧠 Disease Classification

| Class | Description |
|---|---|
| Healthy | Healthy silkworm |
| Pebrine | Pebrine disease |
| Grasserie | Grasserie disease |

## 🌡️ Environmental Monitoring

- Temperature monitoring
- Humidity monitoring
- Fan control
- Simulated heater control
- Web dashboard

## 🏗️ System Architecture

Camera → ESP32-S3 → Image Capture → CNN Classification → Disease Result

DHT22 → Environmental Monitoring → Control Logic → Dashboard

## 👨‍💻 Project Team

Developed as a final-year engineering project.

## 🚀 Project Status

Hardware integration and software development in progress.
