#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DHT.h>
#include <math.h>

// ==========================================
// ECO-SILK EDGE
// ESP32-S3 + OV2640 + DHT22 + SINGLE RELAY
// ==========================================

// WiFi credentials
const char* ssid = "YOUR_WIFI_NAME";
const char* password = "YOUR_WIFI_PASSWORD";

// DHT22
#define DHT_PIN 14
#define DHT_TYPE DHT22
DHT dht(DHT_PIN, DHT_TYPE);

// Single-channel relay
#define FAN_RELAY_PIN 1

// Most 5V relay modules are active LOW.
// Change these if your relay operates oppositely.
#define RELAY_ON LOW
#define RELAY_OFF HIGH

// Fan temperature thresholds
const float FAN_ON_TEMP = 32.0;
const float FAN_OFF_TEMP = 30.0;

// Sensor readings
float temperature = NAN;
float humidity = NAN;

bool sensorValid = false;
bool fanStatus = false;
bool cameraReady = false;

// Latest CNN prediction received from the laptop
String diseasePrediction = "Waiting for Python";
float diseaseConfidence = 0.0;
bool predictionValid = false;

unsigned long previousMillis = 0;
const unsigned long SENSOR_INTERVAL = 2500;  // DHT22: do not poll faster than this

// Require two consecutive valid threshold readings before changing the fan.
// This prevents brief sensor fluctuations from repeatedly switching the relay.
uint8_t highTempConfirmations = 0;
uint8_t lowTempConfirmations = 0;
uint8_t consecutiveSensorErrors = 0;
const uint8_t REQUIRED_CONFIRMATIONS = 2;
const uint8_t SENSOR_ERRORS_BEFORE_WARNING = 2;

// Web server
WebServer server(80);

// ==========================================
// CAMERA PIN CONFIGURATION
// Your previously supplied OV2640 mapping
// ==========================================

#define PWDN_GPIO_NUM    -1
#define RESET_GPIO_NUM   -1
#define XCLK_GPIO_NUM    15
#define SIOD_GPIO_NUM     4
#define SIOC_GPIO_NUM     5

#define Y9_GPIO_NUM      16
#define Y8_GPIO_NUM      17
#define Y7_GPIO_NUM      18
#define Y6_GPIO_NUM      12
#define Y5_GPIO_NUM      10
#define Y4_GPIO_NUM       8
#define Y3_GPIO_NUM       9
#define Y2_GPIO_NUM      11

#define VSYNC_GPIO_NUM    6
#define HREF_GPIO_NUM     7
#define PCLK_GPIO_NUM    13

// ==========================================
// FAN CONTROL
// ==========================================

void setFan(bool state) {
  // Do not write to the relay unless its requested state has changed.
  if (state == fanStatus) return;

  fanStatus = state;
  digitalWrite(FAN_RELAY_PIN, fanStatus ? RELAY_ON : RELAY_OFF);

  Serial.print("FAN STATE CHANGED: ");
  Serial.println(fanStatus ? "ON" : "OFF");
}

void controlFan() {
  // Invalid sensor readings must NEVER force the relay ON or OFF.
  // Keep the last known fan state until valid readings return.
  if (!sensorValid || !isfinite(temperature)) return;

  if (!fanStatus) {
    // Fan is OFF: require two consecutive readings strictly above 32 C.
    lowTempConfirmations = 0;

    if (temperature > FAN_ON_TEMP) {
      if (highTempConfirmations < REQUIRED_CONFIRMATIONS) {
        highTempConfirmations++;
      }

      if (highTempConfirmations >= REQUIRED_CONFIRMATIONS) {
        setFan(true);
        highTempConfirmations = 0;
      }
    } else {
      highTempConfirmations = 0;
    }
  } else {
    // Fan is ON: require two consecutive readings at or below 30 C.
    highTempConfirmations = 0;

    if (temperature <= FAN_OFF_TEMP) {
      if (lowTempConfirmations < REQUIRED_CONFIRMATIONS) {
        lowTempConfirmations++;
      }

      if (lowTempConfirmations >= REQUIRED_CONFIRMATIONS) {
        setFan(false);
        lowTempConfirmations = 0;
      }
    } else {
      lowTempConfirmations = 0;
    }
  }
}

// ==========================================
// DHT22 READING
// ==========================================

void readDHT() {
  // Read at 2.5-second intervals. The DHT library caches readings briefly;
  // reading both values in this interval avoids over-polling the sensor.
  float newHumidity = dht.readHumidity();
  float newTemp = dht.readTemperature();

  if (!isfinite(newTemp) || !isfinite(newHumidity)) {
    if (consecutiveSensorErrors < SENSOR_ERRORS_BEFORE_WARNING) {
      consecutiveSensorErrors++;
    }

    // A single missed sample is treated as transient. After two consecutive
    // failures, report the sensor error, but still leave the relay untouched.
    if (consecutiveSensorErrors >= SENSOR_ERRORS_BEFORE_WARNING) {
      sensorValid = false;
    }

    Serial.println("DHT22 read failed; keeping previous fan state.");
    return;
  }

  consecutiveSensorErrors = 0;
  temperature = newTemp;
  humidity = newHumidity;
  sensorValid = true;

  controlFan();

  Serial.println("-----------------------------");
  Serial.print("Temperature: ");
  Serial.print(temperature, 1);
  Serial.println(" C");
  Serial.print("Humidity: ");
  Serial.print(humidity, 1);
  Serial.println(" %");
  Serial.print("Fan: ");
  Serial.println(fanStatus ? "ON" : "OFF");
  Serial.println("-----------------------------");
}

// ==========================================
// CAMERA INITIALIZATION
// ==========================================

void startCamera() {

  camera_config_t config = {};

  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;

  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;

  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;

  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;

  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;

  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  config.frame_size = FRAMESIZE_QVGA;
  config.jpeg_quality = 12;

  config.fb_count = psramFound() ? 2 : 1;
  config.fb_location = psramFound()
                       ? CAMERA_FB_IN_PSRAM
                       : CAMERA_FB_IN_DRAM;

  config.grab_mode = CAMERA_GRAB_LATEST;

  esp_err_t result = esp_camera_init(&config);

  if (result == ESP_OK) {
    cameraReady = true;
    Serial.println("Camera initialized successfully");
  } else {
    cameraReady = false;
    Serial.printf("Camera initialization failed: 0x%x\n", result);
  }
}

// ==========================================
// DASHBOARD HOME PAGE
// ==========================================

void handleHome() {

  String page = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">

<title>Eco-Silk Edge Dashboard</title>

<style>
body {
  font-family: Arial, sans-serif;
  background: #eef4f0;
  margin: 0;
  padding: 15px;
  text-align: center;
  color: #183b2a;
}

h1 {
  color: #166534;
}

.container {
  max-width: 600px;
  margin: auto;
}

.card {
  background: white;
  margin: 15px 0;
  padding: 18px;
  border-radius: 12px;
  box-shadow: 0 2px 8px #0002;
}

.value {
  font-size: 30px;
  font-weight: bold;
  color: #166534;
}

img {
  width: 100%;
  max-width: 500px;
  border-radius: 10px;
}

.status {
  font-size: 20px;
  font-weight: bold;
}

.on {
  color: #15803d;
}

.off {
  color: #64748b;
}

.error {
  color: #dc2626;
}

.small {
  color: #64748b;
  font-size: 13px;
}
</style>
</head>

<body>

<div class="container">

<h1>Eco-Silk Edge</h1>
<p>Smart Sericulture Monitoring System</p>

<div class="card">
<h2>ESP32 Camera</h2>
<img id="camera" src="/capture" alt="Camera unavailable">
<p class="small">Live camera preview</p>
</div>

<div class="card">
<h2>Temperature</h2>
<div class="value" id="temp">-- °C</div>
</div>

<div class="card">
<h2>Humidity</h2>
<div class="value" id="humidity">-- %</div>
</div>

<div class="card">
<h2>Sensor Status</h2>
<div class="status" id="sensor">Checking...</div>
</div>

<div class="card">
<h2>Fan Status</h2>
<div class="status" id="fan">Checking...</div>
</div>

<div class="card">
<h2>CNN Disease Detection</h2>
<div class="value" id="disease">Waiting for Python...</div>
<p class="small">Confidence: <span id="confidence">--</span></p>
<p class="small" id="cnnStatus">CNN connection: waiting</p>
</div>

<div class="card">
<h3>Control Conditions</h3>
<p>Fan ON: Above 32°C for 2 consecutive readings</p>
<p>Fan OFF: At or below 30°C for 2 consecutive readings</p>
<p>Between 30°C and 32°C: Fan keeps its previous state</p>
</div>

<p class="small">Eco-Silk Edge | ESP32-S3 Monitoring</p>

</div>

<script>

// Get temperature, humidity and fan status
async function updateDashboard() {

  try {

    const response = await fetch('/api?t=' + Date.now(), {
      cache: 'no-store'
    });

    if (!response.ok) {
      throw new Error("API unavailable");
    }

    const data = await response.json();

    if (data.sensorValid) {

      document.getElementById("temp").innerText =
        data.temperature.toFixed(1) + " °C";

      document.getElementById("humidity").innerText =
        data.humidity.toFixed(1) + " %";

      document.getElementById("sensor").innerText =
        "WORKING";

      document.getElementById("sensor").className =
        "status on";

    } else {

      document.getElementById("temp").innerText =
        "Sensor Error";

      document.getElementById("humidity").innerText =
        "Sensor Error";

      document.getElementById("sensor").innerText =
        "CHECK DHT22";

      document.getElementById("sensor").className =
        "status error";
    }

    document.getElementById("fan").innerText =
      data.fan ? "ON" : "OFF";

    document.getElementById("fan").className =
      data.fan ? "status on" : "status off";

    // Latest prediction sent by the laptop CNN program
    if (data.predictionValid) {
      document.getElementById("disease").innerText =
        data.prediction;

      document.getElementById("confidence").innerText =
        Number(data.confidence).toFixed(1) + "%";

      document.getElementById("cnnStatus").innerText =
        "CNN connected - updating";
      document.getElementById("cnnStatus").className =
        "small on";
    } else {
      document.getElementById("disease").innerText =
        "Waiting for Python...";
      document.getElementById("confidence").innerText = "--";
      document.getElementById("cnnStatus").innerText =
        "Start the Python CNN integration script";
      document.getElementById("cnnStatus").className = "small";
    }

  }
  catch(error) {

    document.getElementById("sensor").innerText =
      "Dashboard Connection Error";

    document.getElementById("sensor").className =
      "status error";
  }
}

// Update readings every 2.5 seconds
setInterval(updateDashboard, 2500);
updateDashboard();

// Refresh camera image every 1.5 seconds
setInterval(function() {

  document.getElementById("camera").src =
    "/capture?t=" + Date.now();

}, 1500);

</script>

</body>
</html>
)rawliteral";

  server.send(200, "text/html", page);
}

// ==========================================
// CAMERA CAPTURE
// ==========================================

void handleCapture() {

  if (!cameraReady) {
    server.send(503, "text/plain", "Camera unavailable");
    return;
  }

  camera_fb_t *fb = esp_camera_fb_get();

  if (!fb) {
    server.send(500, "text/plain", "Capture failed");
    return;
  }

  server.setContentLength(fb->len);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "image/jpeg", "");

  server.client().write(fb->buf, fb->len);

  esp_camera_fb_return(fb);
}

// ==========================================
// RECEIVE CNN PREDICTION FROM PYTHON LAPTOP
// POST /prediction
// JSON: {"disease":"Healthy","confidence":96.2}
// ==========================================

void handlePrediction() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json",
                "{\"ok\":false,\"error\":\"Missing JSON body\"}");
    return;
  }

  String body = server.arg("plain");

  int diseaseKey = body.indexOf("\"disease\":");
  int confidenceKey = body.indexOf("\"confidence\":");

  if (diseaseKey < 0 || confidenceKey < 0) {
    server.send(400, "application/json",
                "{\"ok\":false,\"error\":\"Expected disease and confidence\"}");
    return;
  }

  int diseaseStart = body.indexOf('"', diseaseKey + 9);
  int diseaseEnd = body.indexOf('"', diseaseStart + 1);

  if (diseaseStart < 0 || diseaseEnd < 0) {
    server.send(400, "application/json",
                "{\"ok\":false,\"error\":\"Invalid disease value\"}");
    return;
  }

  String newDisease = body.substring(diseaseStart + 1, diseaseEnd);
  newDisease.trim();

  if (newDisease != "Healthy" &&
      newDisease != "Pebrine" &&
      newDisease != "Grasserie") {
    server.send(400, "application/json",
                "{\"ok\":false,\"error\":\"Unknown disease class\"}");
    return;
  }

  int confidenceStart = body.indexOf(':', confidenceKey) + 1;
  int confidenceEnd = body.indexOf(',', confidenceStart);
  if (confidenceEnd < 0) {
    confidenceEnd = body.indexOf('}', confidenceStart);
  }

  if (confidenceStart <= 0 || confidenceEnd < 0) {
    server.send(400, "application/json",
                "{\"ok\":false,\"error\":\"Invalid confidence value\"}");
    return;
  }

  float newConfidence = body.substring(confidenceStart, confidenceEnd).toFloat();

  if (!isfinite(newConfidence) || newConfidence < 0.0 || newConfidence > 100.0) {
    server.send(400, "application/json",
                "{\"ok\":false,\"error\":\"Confidence must be 0 to 100\"}");
    return;
  }

  diseasePrediction = newDisease;
  diseaseConfidence = newConfidence;
  predictionValid = true;

  Serial.print("CNN prediction: ");
  Serial.print(diseasePrediction);
  Serial.print(" | Confidence: ");
  Serial.println(diseaseConfidence, 1);

  server.send(200, "application/json", "{\"ok\":true}");
}

// ==========================================
// SENSOR API FOR CHROME DASHBOARD
// ==========================================

void handleAPI() {

  String json = "{";

  json += "\"sensorValid\":";
  json += sensorValid ? "true" : "false";

  json += ",\"temperature\":";
  json += sensorValid ? String(temperature, 1) : "null";

  json += ",\"humidity\":";
  json += sensorValid ? String(humidity, 1) : "null";

  json += ",\"fan\":";
  json += fanStatus ? "true" : "false";

  json += ",\"predictionValid\":";
  json += predictionValid ? "true" : "false";

  json += ",\"prediction\":\"";
  json += diseasePrediction;
  json += "\"";

  json += ",\"confidence\":";
  json += String(diseaseConfidence, 1);

  json += "}";

  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

// ==========================================
// SETUP
// ==========================================

void setup() {

  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("ECO-SILK EDGE STARTING");

  // Initialize relay
  pinMode(FAN_RELAY_PIN, OUTPUT);
  digitalWrite(FAN_RELAY_PIN, RELAY_OFF);

  // Initialize DHT22
  dht.begin();

  Serial.println("DHT22 initialized");

  // Initialize camera
  startCamera();

  // Give DHT22 time before first reading
  delay(2000);
  readDHT();

  // WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  Serial.print("Connecting to WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi connected!");

  Serial.print("Dashboard IP: ");
  Serial.println(WiFi.localIP());

  // Web server routes
  server.on("/", HTTP_GET, handleHome);
  server.on("/capture", HTTP_GET, handleCapture);
  server.on("/api", HTTP_GET, handleAPI);
  server.on("/prediction", HTTP_POST, handlePrediction);

  server.begin();

  Serial.println("Web server started");
  Serial.println("Eco-Silk Edge READY");
}

// ==========================================
// MAIN LOOP
// ==========================================

void loop() {

  server.handleClient();

  if (millis() - previousMillis >= SENSOR_INTERVAL) {

    previousMillis = millis();

    readDHT();
  }
}