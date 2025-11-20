#include <WiFi.h>
#include <WiFiUdp.h>
#include "esp_camera.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "base64.h"

// ----------------------
// WIFI
// ----------------------
const char* ssid = "DIGIFIBRA-P9zt";
const char* password = "4t5Z7y3YutEC";

// ----------------------
// FIRESTORE CONFIG
// ----------------------
String apiKey     = "AIzaSyBHTeOdFk1BlxFirAU1eFHjonV7YjrlpO4";
String projectID  = "qubi-d3588";
String docID      = "BX001_CAM";

// ----------------------
// UDP RECEPTOR
// ----------------------
WiFiUDP Udp;
const int udpPort = 4210;

// ----------------------
// PINES ESP32-CAM
// ----------------------
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

// ----------------------
// ENVÍO FIRESTORE
// ----------------------
void enviarFotoFirestore(String base64img) {
  HTTPClient http;

  String url = "https://firestore.googleapis.com/v1/projects/" + projectID +
               "/databases/(default)/documents/camaras/" + docID +
               "?key=" + apiKey;

  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  DynamicJsonDocument doc(40000);
  JsonObject fields = doc.createNestedObject("fields");

  fields["imagenBase64"]["stringValue"] = base64img;
  fields["timestamp"]["integerValue"]   = millis();

  String json;
  serializeJson(doc, json);

  Serial.println("📤 Enviando a Firestore...");
  int code = http.PATCH(json);
  Serial.printf("📡 Código: %d\n", code);

  if (code > 0) Serial.println(http.getString());
  http.end();
}

// ----------------------
// INICIAR CÁMARA
// ----------------------
void iniciarCamara() {
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
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  config.frame_size   = FRAMESIZE_HD;
  config.jpeg_quality = 15;
  config.fb_count     = 1;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("❌ Error init cam: 0x%x", err);
    return;
  }

  Serial.println("📷 Cámara lista.");
}

// ----------------------
// SETUP
// ----------------------
void setup() {
  Serial.begin(115200);
  pinMode(4, OUTPUT);
  digitalWrite(4, LOW);

  WiFi.begin(ssid, password);
  Serial.println("Conectando WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
  }
  Serial.println("\n📶 WiFi listo");
  Serial.println(WiFi.localIP());

  iniciarCamara();

  // Iniciar UDP
  Udp.begin(udpPort);
  Serial.printf("📡 Esperando UDP en puerto %d...\n", udpPort);
}

// ----------------------
// FUNCIÓN TOMAR FOTO
// ----------------------
void tomarYEnviarFoto() {

  digitalWrite(4, HIGH);
  delay(300);

  camera_fb_t* fbClean = esp_camera_fb_get();
  if (fbClean) esp_camera_fb_return(fbClean);

  delay(80);

  camera_fb_t* fb = esp_camera_fb_get();
  digitalWrite(4, LOW);

  if (!fb) {
    Serial.println("❌ Error capturando");
    return;
  }

  String imgBase64 = base64::encode(fb->buf, fb->len);
  esp_camera_fb_return(fb);

  Serial.println("📦 Imagen lista, subiendo...");
  enviarFotoFirestore(imgBase64);
}

// ----------------------
// LOOP
// ----------------------
void loop() {
  int packetSize = Udp.parsePacket();
  if (packetSize) {
    char incoming[10];
    int len = Udp.read(incoming, 10);
    incoming[len] = 0;

    Serial.print("📨 Señal: ");
    Serial.println(incoming);

    if (String(incoming) == "HIGH") {
      Serial.println("🔫 TRIGGER → Tomando foto...");
      tomarYEnviarFoto();
    }
  }
}
