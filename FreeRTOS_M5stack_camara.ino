#include <WiFi.h>
#include <WiFiUdp.h>
#include <M5Stack.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>

// -------------------------
// CONFIG
// -------------------------
const char* ssid = "DIGIFIBRA-P9zt";
const char* password = "4t5Z7y3YutEC";

const char* esp32cam_ip = "192.168.1.149";
const int udpPort = 4210;
WiFiUDP Udp;

String apiKey    = "AIzaSyBHTeOdFk1BlxFirAU1eFHjonV7YjrlpO4";
String projectID = "qubi-d3588";

String buzonNumeroIdentificador = "BX001";
String usuarioId = "M5StackCam";


static QueueHandle_t qBtn = nullptr;          
static SemaphoreHandle_t lcdMutex = nullptr;  

static inline void lcdLock()   { xSemaphoreTake(lcdMutex, portMAX_DELAY); }
static inline void lcdUnlock() { xSemaphoreGive(lcdMutex); }


static void syncTime() {
  configTime(3600, 0, "pool.ntp.org", "time.nist.gov");
  time_t now = 0;
  int retries = 0;
  while (now < 1700000000 && retries < 30) {
    vTaskDelay(pdMS_TO_TICKS(250));
    time(&now);
    retries++;
  }
}

static String isoTimestampUTC() {
  time_t now;
  time(&now);
  struct tm tmUtc;
  gmtime_r(&now, &tmUtc);

  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmUtc);
  return String(buf);
}

static String generarNombreImagen() {
  uint32_t r = (uint32_t)esp_random();
  time_t now; time(&now);

  char hexbuf[9];
  snprintf(hexbuf, sizeof(hexbuf), "%08X", (unsigned)r);

  return buzonNumeroIdentificador + "_" + String((unsigned long)now) + "_" + String(hexbuf) + ".jpg";
}

static void enviarUDP(const String& mensaje) {
  Udp.beginPacket(esp32cam_ip, udpPort);
  Udp.print(mensaje);
  Udp.endPacket();
}

static bool crearNotificacionSolicitud(const String& nombreImagen) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  String url = "https://firestore.googleapis.com/v1/projects/" + projectID +
               "/databases/(default)/documents/notificaciones?key=" + apiKey;

  if (!http.begin(client, url)) return false;
  http.addHeader("Content-Type", "application/json");

  String titulo  = "🔔 Solicitud de Apertura en " + buzonNumeroIdentificador;
  String mensaje = "Se ha solicitado una apertura en tu buzón " + buzonNumeroIdentificador;

  JsonDocument doc;
  JsonObject fields = doc["fields"].to<JsonObject>();
  fields["buzonNumeroIdentificador"]["stringValue"] = buzonNumeroIdentificador;
  fields["fecha"]["timestampValue"] = isoTimestampUTC();
  fields["leido"]["booleanValue"] = false;
  fields["mensaje"]["stringValue"] = mensaje;
  fields["tipo"]["stringValue"] = "solicitud";
  fields["titulo"]["stringValue"] = titulo;
  fields["usuarioId"]["stringValue"] = usuarioId;
  fields["imagen"]["stringValue"] = nombreImagen;

  String json;
  serializeJson(doc, json);

  int code = http.POST(json);
  http.end();
  return (code == 200 || code == 201);
}


static void pantallaInicio() {
  M5.Lcd.clear();
  M5.Lcd.setTextSize(3);
  M5.Lcd.setTextColor(WHITE, BLACK);
  M5.Lcd.setCursor(20, 100);
  M5.Lcd.println("PULSE EL BOTON B");
}

static void pantallaFoto() {
  M5.Lcd.clear();
  M5.Lcd.setTextSize(3);
  M5.Lcd.setTextColor(GREEN, BLACK);
  M5.Lcd.setCursor(10, 60);  M5.Lcd.println("HACIENDO");
  M5.Lcd.setCursor(10, 100); M5.Lcd.println("FOTO...");
}


static void TaskBoton(void* pv) {
  (void)pv;
  bool last = false;
  uint32_t lastMs = 0;
  const uint32_t debounceMs = 250;

  for (;;) {
    M5.update();
    bool now = M5.BtnB.isPressed();
    uint32_t t = millis();

    if (now && !last && (t - lastMs) > debounceMs) {
   
      uint8_t ev = 1;
      xQueueSend(qBtn, &ev, 0);
      lastMs = t;
    }

    last = now;
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}


static void TaskTrabajo(void* pv) {
  (void)pv;
  uint8_t ev;

  for (;;) {
    
    if (xQueueReceive(qBtn, &ev, portMAX_DELAY) == pdTRUE) {

      
      lcdLock();
      pantallaFoto();
      lcdUnlock();

      // Nombre + pequeña espera
      String nombre = generarNombreImagen();
      vTaskDelay(pdMS_TO_TICKS(2000));

      
      enviarUDP("CAPTURE:" + nombre);

      
      bool ok = crearNotificacionSolicitud(nombre);

      
      lcdLock();
      M5.Lcd.setTextSize(2);
      M5.Lcd.setTextColor(WHITE, BLACK);
      M5.Lcd.setCursor(10, 10);
      M5.Lcd.printf("Noti: %s", ok ? "OK" : "FALLO");
      M5.Lcd.setCursor(10, 30);
      M5.Lcd.printf("Img: %s", nombre.c_str());
      lcdUnlock();

      vTaskDelay(pdMS_TO_TICKS(1800));

      lcdLock();
      pantallaInicio();
      lcdUnlock();
    }
  }
}


void setup() {
  M5.begin();
  Serial.begin(115200);

  lcdMutex = xSemaphoreCreateMutex();
  qBtn = xQueueCreate(5, sizeof(uint8_t)); 

  lcdLock();
  M5.Lcd.setTextSize(2);
  M5.Lcd.print("Conectando WiFi...");
  lcdUnlock();

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    vTaskDelay(pdMS_TO_TICKS(300));
    lcdLock(); M5.Lcd.print("."); lcdUnlock();
  }

  Udp.begin(udpPort);
  syncTime();

  lcdLock();
  pantallaInicio();
  lcdUnlock();

  // 2 tareas
  xTaskCreatePinnedToCore(TaskBoton,   "Boton",   4096, nullptr, 2, nullptr, 1);
  xTaskCreatePinnedToCore(TaskTrabajo, "Trabajo", 8192, nullptr, 1, nullptr, 1);
}

void loop() {
  
  vTaskDelay(pdMS_TO_TICKS(1000));
}
