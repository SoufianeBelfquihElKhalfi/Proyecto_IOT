/*
  ESP32-CAM (AI Thinker / ESP32-CAM-MB) - Streaming MJPEG + página web
  + IP estática
  + Flash (LED blanco) siempre encendido

  Board: AI Thinker ESP32-CAM
  PSRAM: Enabled
  Monitor serie: 115200
*/

#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>

// --------- WiFi ----------
const char* WIFI_SSID = "DIGIFIBRA-P9zt";
const char* WIFI_PASS = "4t5Z7y3YutEC";

// --------- IP estática ----------
IPAddress local_IP(192, 168, 1, 154);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress dns1(192, 168, 1, 1);
IPAddress dns2(8, 8, 4, 4);

// --------- Cámara (AI Thinker ESP32-CAM) ----------
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// --------- Flash LED (blanco) del ESP32-CAM ----------
#define FLASH_LED_PIN 4

WebServer server(80);

static const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="es">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>ESP32-CAM Live</title>
  <style>
    body{font-family:system-ui,Arial;margin:0;padding:16px;background:#0b0c10;color:#e5e7eb}
    .card{max-width:900px;margin:auto;background:#111827;border-radius:14px;padding:16px}
    h1{margin:0 0 12px 0;font-size:18px}
    img{width:100%;border-radius:12px;background:#000}
    .row{display:flex;gap:8px;flex-wrap:wrap;margin-top:12px}
    button{padding:10px 12px;border-radius:10px;border:0;cursor:pointer}
  </style>
</head>
<body>
  <div class="card">
    <h1>ESP32-CAM - Video en directo (MJPEG)</h1>
    <img id="stream" src="/stream" alt="stream">
    <div class="row">
      <button onclick="setRes('qvga')">QVGA</button>
      <button onclick="setRes('vga')">VGA</button>
      <button onclick="setRes('svga')">SVGA</button>
      <button onclick="setRes('xga')">XGA</button>
      <button onclick="location.reload()">Recargar</button>
    </div>
    <p>Endpoint de stream: <code>/stream</code></p>
  </div>

<script>
async function setRes(r){
  await fetch('/control?var=framesize&val=' + ({
    qvga:5, vga:6, svga:7, xga:8
  }[r] ?? 5));
}
</script>
</body>
</html>
)HTML";

void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleControl() {
  if (!server.hasArg("var") || !server.hasArg("val")) {
    server.send(400, "text/plain", "Missing var/val");
    return;
  }
  String var = server.arg("var");
  int val = server.arg("val").toInt();

  sensor_t * s = esp_camera_sensor_get();
  int res = 0;

  if (var == "framesize")       res = s->set_framesize(s, (framesize_t)val);
  else if (var == "quality")    res = s->set_quality(s, val);
  else if (var == "brightness") res = s->set_brightness(s, val);
  else if (var == "contrast")   res = s->set_contrast(s, val);
  else if (var == "saturation") res = s->set_saturation(s, val);
  else {
    server.send(400, "text/plain", "Unknown var");
    return;
  }

  server.send(res == 0 ? 200 : 500, "text/plain", res == 0 ? "OK" : "ERR");
}

void handleStream() {
  WiFiClient client = server.client();

  client.print(
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
    "Cache-Control: no-cache\r\n"
    "Connection: close\r\n\r\n"
  );

  while (client.connected()) {
    camera_fb_t * fb = esp_camera_fb_get();
    if (!fb) break;

    client.print("--frame\r\n");
    client.print("Content-Type: image/jpeg\r\n");
    client.print("Content-Length: " + String(fb->len) + "\r\n\r\n");
    client.write(fb->buf, fb->len);
    client.print("\r\n");

    esp_camera_fb_return(fb);
    delay(10);
  }
}

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(false);

  // --- Flash LED siempre encendido ---
  pinMode(FLASH_LED_PIN, OUTPUT);
  digitalWrite(FLASH_LED_PIN, HIGH); // encendido fijo

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;

  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;

  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size   = FRAMESIZE_VGA;
    config.jpeg_quality = 12;
    config.fb_count     = 2;
  } else {
    config.frame_size   = FRAMESIZE_QVGA;
    config.jpeg_quality = 15;
    config.fb_count     = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Error init cámara: 0x%x\n", err);
    while (true) delay(1000);
  }

  WiFi.mode(WIFI_STA);

  // --- IP estática (ANTES de WiFi.begin) ---
  if (!WiFi.config(local_IP, gateway, subnet, dns1, dns2)) {
    Serial.println("Error configurando IP estática");
  }

  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.print("Conectando WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi OK");
  Serial.print("Abre: http://");
  Serial.println(WiFi.localIP());
  Serial.print("Stream: http://");
  Serial.print(WiFi.localIP());
  Serial.println("/stream");

  server.on("/", HTTP_GET, handleRoot);
  server.on("/control", HTTP_GET, handleControl);
  server.on("/stream", HTTP_GET, handleStream);
  server.begin();
}

void loop() {
  server.handleClient();
}
