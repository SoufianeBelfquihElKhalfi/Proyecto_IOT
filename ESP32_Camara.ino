#include <WiFi.h>
#include <WiFiUdp.h>
#include "esp_camera.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>

// ----------------------
// WIFI
// ----------------------
const char* ssid = "TP-LINK_6CAE";
const char* password = "41422915";

// ----------------------
// FIREBASE (REST)
// ----------------------
static const char* API_KEY = "AIzaSyBHTeOdFk1BlxFirAU1eFHjonV7YjrlpO4";
// Usa el bucket que te sale en Firebase Console
static const char* STORAGE_BUCKET = "qubi-d3588.firebasestorage.app";

// ----------------------
// UDP RECEPTOR
// ----------------------
WiFiUDP Udp;
const int udpPort = 4210;

// ----------------------
// PINES ESP32-CAM (AI Thinker)
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

// Flash LED
#define FLASH_GPIO_NUM 4

// ----------------------
// TOKEN (Firebase Auth)
// ----------------------
String g_idToken = "";
String g_refreshToken = "";
unsigned long g_tokenExpMillis = 0; // millis() cuando expira (aprox)

// ----------------------
// HELPERS
// ----------------------
String urlEncode(const String &s) {
  String out;
  const char *hex = "0123456789ABCDEF";
  for (size_t i = 0; i < s.length(); i++) {
    uint8_t c = (uint8_t)s[i];
    if (('a' <= c && c <= 'z') ||
        ('A' <= c && c <= 'Z') ||
        ('0' <= c && c <= '9') ||
        c == '-' || c == '_' || c == '.' || c == '~') {
      out += (char)c;
    } else {
      out += '%';
      out += hex[(c >> 4) & 0xF];
      out += hex[c & 0xF];
    }
  }
  return out;
}

bool httpsPOSTJson(const String& url, const String& jsonBody, int &httpCodeOut, String &respOut) {
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(20000);

  HTTPClient http;
  http.setReuse(false);
  http.setTimeout(20000);
  http.useHTTP10(true);

  if (!http.begin(client, url)) return false;
  http.addHeader("Content-Type", "application/json");

  int code = http.POST((uint8_t*)jsonBody.c_str(), jsonBody.length());
  httpCodeOut = code;
  respOut = (code > 0) ? http.getString() : "";
  http.end();
  return (code > 0);
}

bool httpsPOSTForm(const String& url, const String& formBody, int &httpCodeOut, String &respOut) {
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(20000);

  HTTPClient http;
  http.setReuse(false);
  http.setTimeout(20000);
  http.useHTTP10(true);

  if (!http.begin(client, url)) return false;
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  int code = http.POST(formBody);
  httpCodeOut = code;
  respOut = (code > 0) ? http.getString() : "";
  http.end();
  return (code > 0);
}

bool tokenValido() {
  if (g_idToken.length() == 0) return false;
  if (millis() + 60000UL >= g_tokenExpMillis) return false;
  return true;
}

// ----------------------
// AUTH ANÓNIMO
// ----------------------
bool firebaseAnonSignUp() {
  String url = String("https://identitytoolkit.googleapis.com/v1/accounts:signUp?key=") + API_KEY;
  String body = "{\"returnSecureToken\":true}";

  int code; String resp;
  if (!httpsPOSTJson(url, body, code, resp)) {
    Serial.println("❌ Error HTTPS signUp (no request)");
    return false;
  }

  Serial.printf("Auth signUp code: %d\n", code);
  if (code < 200 || code >= 300) {
    Serial.println(resp);
    return false;
  }

  DynamicJsonDocument doc(4096);
  if (deserializeJson(doc, resp)) {
    Serial.println("❌ Error parseando signUp JSON");
    return false;
  }

  g_idToken = doc["idToken"].as<String>();
  g_refreshToken = doc["refreshToken"].as<String>();
  long expiresInSec = doc["expiresIn"].as<long>();

  if (g_idToken.length() == 0 || g_refreshToken.length() == 0) {
    Serial.println("❌ signUp sin tokens");
    return false;
  }

  g_tokenExpMillis = millis() + (unsigned long)expiresInSec * 1000UL;
  Serial.println("✅ Firebase Auth anónimo OK (token obtenido)");
  return true;
}

bool firebaseRefreshToken() {
  if (g_refreshToken.length() == 0) return false;

  String url = String("https://securetoken.googleapis.com/v1/token?key=") + API_KEY;
  String form = "grant_type=refresh_token&refresh_token=" + urlEncode(g_refreshToken);

  int code; String resp;
  if (!httpsPOSTForm(url, form, code, resp)) {
    Serial.println("❌ Error HTTPS refresh (no request)");
    return false;
  }

  Serial.printf("Auth refresh code: %d\n", code);
  if (code < 200 || code >= 300) {
    Serial.println(resp);
    return false;
  }

  DynamicJsonDocument doc(4096);
  if (deserializeJson(doc, resp)) {
    Serial.println("❌ Error parseando refresh JSON");
    return false;
  }

  g_idToken = doc["access_token"].as<String>();
  String newRefresh = doc["refresh_token"].as<String>();
  long expiresInSec = doc["expires_in"].as<long>();

  if (g_idToken.length() == 0) {
    Serial.println("❌ refresh sin access_token");
    return false;
  }
  if (newRefresh.length() > 0) g_refreshToken = newRefresh;

  g_tokenExpMillis = millis() + (unsigned long)expiresInSec * 1000UL;
  Serial.println("✅ Token refrescado");
  return true;
}

bool asegurarToken() {
  if (tokenValido()) return true;
  if (g_refreshToken.length() > 0) {
    if (firebaseRefreshToken()) return true;
  }
  return firebaseAnonSignUp();
}

// ----------------------
// SUBIR JPG A STORAGE (REST)
// NOTA: data NO const por compatibilidad con core ESP32 2.0.0
// ----------------------
bool subirJpgAStorage(uint8_t* data, size_t len, const String& remotePath) {
  if (!asegurarToken()) {
    Serial.println("❌ No hay token válido para subir a Storage");
    return false;
  }

  String encodedName = urlEncode(remotePath);
  String url = String("https://firebasestorage.googleapis.com/v0/b/") +
               STORAGE_BUCKET +
               "/o?uploadType=media&name=" + encodedName;

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(20000);

  HTTPClient http;
  http.setReuse(false);
  http.setTimeout(20000);
  http.useHTTP10(true);

  if (!http.begin(client, url)) {
    Serial.println("❌ http.begin() falló");
    return false;
  }

  http.addHeader("Content-Type", "image/jpeg");
  http.addHeader("Authorization", "Bearer " + g_idToken);

  int code = http.sendRequest("POST", data, len);
  Serial.printf("📡 Storage HTTP code: %d\n", code);

  if (code > 0) {
    String resp = http.getString();
    Serial.println(resp);
  }

  http.end();
  return (code == 200 || code == 201);
}

// ----------------------
// INICIAR CÁMARA
// ----------------------
void iniciarCamara() {
  camera_config_t c;
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer   = LEDC_TIMER_0;
  c.pin_d0       = Y2_GPIO_NUM;
  c.pin_d1       = Y3_GPIO_NUM;
  c.pin_d2       = Y4_GPIO_NUM;
  c.pin_d3       = Y5_GPIO_NUM;
  c.pin_d4       = Y6_GPIO_NUM;
  c.pin_d5       = Y7_GPIO_NUM;
  c.pin_d6       = Y8_GPIO_NUM;
  c.pin_d7       = Y9_GPIO_NUM;
  c.pin_xclk     = XCLK_GPIO_NUM;
  c.pin_pclk     = PCLK_GPIO_NUM;
  c.pin_vsync    = VSYNC_GPIO_NUM;
  c.pin_href     = HREF_GPIO_NUM;

  // Tu core usa pin_sscb_*
  c.pin_sscb_sda = SIOD_GPIO_NUM;
  c.pin_sscb_scl = SIOC_GPIO_NUM;

  c.pin_pwdn     = PWDN_GPIO_NUM;
  c.pin_reset    = RESET_GPIO_NUM;
  c.xclk_freq_hz = 20000000;
  c.pixel_format = PIXFORMAT_JPEG;

  c.frame_size   = FRAMESIZE_VGA; // más fiable
  c.jpeg_quality = 12;
  c.fb_count     = 1;

  esp_err_t err = esp_camera_init(&c);
  if (err != ESP_OK) {
    Serial.printf("❌ Error init cam: 0x%x\n", err);
    return;
  }

  Serial.println("📷 Cámara lista.");
}

// ----------------------
// TOMAR Y SUBIR (libera fb antes de HTTPS)
// ----------------------
void tomarYSubir(const String& filename) {
  digitalWrite(FLASH_GPIO_NUM, HIGH);
  delay(250);

  camera_fb_t* fbClean = esp_camera_fb_get();
  if (fbClean) esp_camera_fb_return(fbClean);
  delay(80);

  camera_fb_t* fb = esp_camera_fb_get();
  digitalWrite(FLASH_GPIO_NUM, LOW);

  if (!fb) {
    Serial.println("❌ Error capturando");
    return;
  }

  String name = filename;
  if (!name.endsWith(".jpg") && !name.endsWith(".jpeg")) name += ".jpg";
  String remotePath = "notificaciones/" + name;

  size_t len = fb->len;

  uint8_t* buf = (uint8_t*)ps_malloc(len);
  if (!buf) buf = (uint8_t*)malloc(len);

  if (!buf) {
    Serial.println("❌ Sin memoria para copiar JPG");
    esp_camera_fb_return(fb);
    return;
  }

  memcpy(buf, fb->buf, len);
  esp_camera_fb_return(fb);

  Serial.printf("📤 Subiendo a Storage: %s (%u bytes)\n", remotePath.c_str(), (unsigned)len);
  Serial.printf("Heap libre: %u | PSRAM libre: %u\n", ESP.getFreeHeap(), ESP.getFreePsram());

  bool ok = subirJpgAStorage(buf, len, remotePath);
  free(buf);

  if (ok) Serial.println("✅ Subida OK");
  else    Serial.println("❌ Subida FALLÓ");
}

// ----------------------
// SETUP
// ----------------------
void setup() {
  Serial.begin(115200);

  pinMode(FLASH_GPIO_NUM, OUTPUT);
  digitalWrite(FLASH_GPIO_NUM, LOW);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  WiFi.setSleep(false);

  Serial.println("Conectando WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
  }
  Serial.println("\n📶 WiFi listo");
  Serial.println(WiFi.localIP());

  asegurarToken();
  iniciarCamara();

  Udp.begin(udpPort);
  Serial.printf("📡 Esperando UDP en puerto %d...\n", udpPort);
}

// ----------------------
// LOOP
// ----------------------
void loop() {
  int packetSize = Udp.parsePacket();
  if (packetSize) {
    char incoming[256];
    int len = Udp.read(incoming, sizeof(incoming) - 1);
    if (len <= 0) return;
    incoming[len] = 0;

    String msg = String(incoming);
    msg.trim();

    Serial.print("📨 UDP: ");
    Serial.println(msg);

    if (msg.startsWith("CAPTURE:")) {
      String filename = msg.substring(String("CAPTURE:").length());
      filename.trim();
      if (filename.length() > 0) {
        Serial.println("📸 Trigger → Tomando foto...");
        tomarYSubir(filename);
      }
    }
  }

  if (!tokenValido() && g_refreshToken.length() > 0) {
    firebaseRefreshToken();
  }

  delay(10);
}
