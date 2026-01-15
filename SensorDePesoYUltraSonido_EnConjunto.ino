/**
 *  Codigo de Peso y Ultra sonido con FreeRTOS.
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include "HX711.h"
#include <math.h>

// -------------------------
// CONFIGURAR WIFI
// -------------------------
const char* ssid = "DIGIFIBRA-P9zt";
const char* password = "4t5Z7y3YutEC";

// -------------------------
// CONFIGURAR FIRESTORE
// -------------------------
String apiKey        = "AIzaSyBHTeOdFk1BlxFirAU1eFHjonV7YjrlpO4";
String projectID     = "qubi-d3588";
String identificador = "BX001";  // numeroIdentificador del buzón

// -------------------------
// CONFIGURAR HC-SR04
// -------------------------
const int TRIG_PIN = 5;
const int ECHO_PIN = 16;
const float ALTURA_SIN_PAQUETE_CM = 56.45f;

// -------------------------
// CONFIGURAR HX711 (sensor peso)
// -------------------------
const int DT_PIN  = 34;
const int SCK_PIN = 26;

HX711 balanza;

// Factor de escala (ajusta al tuyo)
const float ESCALA = -505.320862f;

// Umbral de peso para considerar que hay paquete (en kg)
const float UMBRAL_PAQUETE = 0.05f;

// Cambio mínimo de peso para reenviar (en kg)
const float DELTA_ENVIO_KG = 0.02f;

// -------------------------
// VARIABLES DE ESTADO (solo internas, NO Firestore)
// -------------------------
bool  paquetePresente      = false;
float ultimoPesoEnviadoKg  = 0.0f;
float ultimaAlturaEnviada  = 0.0f;

// ======================================================
// FreeRTOS helpers: reemplazo exacto de delay()
// ======================================================
static inline void delayRTOS(uint32_t ms) {
  vTaskDelay(pdMS_TO_TICKS(ms));
}

// -------------------------
// FUNCIONES AUXILIARES
// -------------------------
void conectarWiFi(uint32_t timeoutMs = 4000) {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.print("WiFi desconectado. Reconectando");
  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < timeoutMs) {
    Serial.print(".");
    delayRTOS(200); // antes era delay(200)
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi reconectado!");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nNo se pudo reconectar (timeout).");
  }
}

// --------------------------
// MEDIR ALTURA CON ULTRASONIDO (cm) - versión simple y más tolerante
// --------------------------
float leerAlturaCm() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 40000); // 40 ms

  if (duration == 0) {
    Serial.println("⚠ No se recibio eco del HC-SR04, uso la última altura conocida");
    return ultimaAlturaEnviada;
  }

  float distance = duration * 0.034f / 2.0f; // cm
  float altura   = ALTURA_SIN_PAQUETE_CM - distance;
  if (altura < 0.0f) altura = 0.0f;

  Serial.print("📏 Altura medida: ");
  Serial.print(altura);
  Serial.println(" cm");

  return altura;
}

// --------------------------
// ACTUALIZAR BUZONES (solo peso + altura)
// --------------------------
bool actualizarBuzonesPorIdentificador(float pesoKg, float alturaCm) {
  if (WiFi.status() != WL_CONNECTED) {
    conectarWiFi();
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Sin WiFi, no se envía a Firestore");
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  String url = "https://firestore.googleapis.com/v1/projects/" + projectID +
               "/databases/(default)/documents:runQuery?key=" + apiKey;

  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");

  DynamicJsonDocument query(500);
  JsonObject structured = query.createNestedObject("structuredQuery");
  JsonArray from = structured.createNestedArray("from");
  JsonObject coll = from.createNestedObject();
  coll["collectionId"] = "buzones";

  JsonObject where = structured.createNestedObject("where");
  JsonObject fieldFilter = where.createNestedObject("fieldFilter");
  fieldFilter["field"]["fieldPath"] = "numeroIdentificador";
  fieldFilter["op"] = "EQUAL";
  fieldFilter["value"]["stringValue"] = identificador;

  String requestBody;
  serializeJson(query, requestBody);

  int httpResponse = http.POST(requestBody);
  Serial.print("runQuery code: ");
  Serial.println(httpResponse);

  if (httpResponse <= 0) {
    Serial.println("Error en consulta Firestore");
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  DynamicJsonDocument doc(8000);
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.print("Error parseando JSON runQuery: ");
    Serial.println(err.c_str());
    return false;
  }

  bool alMenosUnoActualizado = false;

  for (JsonVariant v : doc.as<JsonArray>()) {
    if (!v.containsKey("document")) continue;

    String fullName = v["document"]["name"].as<String>();
    int lastSlash = fullName.lastIndexOf('/');
    String docID = fullName.substring(lastSlash + 1);

    Serial.print("Buzón encontrado → ");
    Serial.println(docID);

    WiFiClientSecure client2;
    client2.setInsecure();

    HTTPClient http2;
    String urlUpdate = "https://firestore.googleapis.com/v1/projects/" + projectID +
                       "/databases/(default)/documents/buzones/" + docID +
                       "?updateMask.fieldPaths=peso&updateMask.fieldPaths=altura&key=" + apiKey;

    http2.begin(client2, urlUpdate);
    http2.addHeader("Content-Type", "application/json");

    DynamicJsonDocument updateDoc(512);
    JsonObject fields = updateDoc.createNestedObject("fields");

    JsonObject pesoObj = fields.createNestedObject("peso");
    pesoObj["doubleValue"] = pesoKg;

    JsonObject alturaObj = fields.createNestedObject("altura");
    alturaObj["doubleValue"] = alturaCm;

    String updateBody;
    serializeJson(updateDoc, updateBody);

    Serial.println("PATCH body:");
    Serial.println(updateBody);

    int updateResponse = http2.PATCH(updateBody);
    Serial.print("PATCH Firestore → Code: ");
    Serial.println(updateResponse);
    if (updateResponse > 0) {
      Serial.println(http2.getString());
    }

    if (updateResponse >= 200 && updateResponse < 300) {
      alMenosUnoActualizado = true;
    }

    http2.end();
  }

  if (!alMenosUnoActualizado) {
    Serial.println("⚠ Ningún documento se actualizó correctamente.");
  }

  return alMenosUnoActualizado;
}

// --------------------------
// REGISTRAR NOTIFICACIÓN EN FIRESTORE (con TIPO)
// --------------------------
void registrarNotificacion(String buzonId, String titulo, String mensaje, String tipo) {
  if (WiFi.status() != WL_CONNECTED) {
    conectarWiFi();
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Sin WiFi, no se envía notificación");
    return;
  }

  time_t now;
  time(&now);
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);

  char buffer[30];
  strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  String url = "https://firestore.googleapis.com/v1/projects/" + projectID +
               "/databases/(default)/documents/notificaciones?key=" + apiKey;

  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");

  DynamicJsonDocument doc(650);
  JsonObject fields = doc.createNestedObject("fields");
  fields["buzonNumeroIdentificador"]["stringValue"] = buzonId;
  fields["mensaje"]["stringValue"] = mensaje;
  fields["titulo"]["stringValue"] = titulo;
  fields["tipo"]["stringValue"] = tipo;
  fields["leido"]["booleanValue"] = false;
  fields["usuarioId"]["stringValue"] = "SensorPesoAltura";
  fields["fecha"]["timestampValue"] = String(buffer);

  String payload;
  serializeJson(doc, payload);

  int code = http.POST(payload);
  Serial.print("POST Firestore Notificacion → Code: ");
  Serial.println(code);
  if (code > 0) Serial.println(http.getString());

  http.end();
}

// ======================================================
// TAREAS
// ======================================================

// Esta tarea reproduce tu loop() “tal cual”.
void taskMainLogic(void* pv) {
  (void)pv;

  for (;;) {
    float peso_g  = balanza.get_units(5);
    float peso_kg = peso_g / 1000.0f;
    if (peso_kg < 0.0f) peso_kg = 0.0f;

    bool hayPaquete = (peso_kg > UMBRAL_PAQUETE);

    Serial.print("Peso: ");
    Serial.print(peso_kg);
    Serial.print(" kg | hayPaquete: ");
    Serial.println(hayPaquete ? "true" : "false");

    // 1) NO había paquete -> AHORA hay: peso + altura juntos
    if (hayPaquete && !paquetePresente) {
      Serial.println("📦 Paquete detectado. Esperando 500 ms para estabilizar...");
      delayRTOS(500); // antes delay(500)

      float peso_g_final  = balanza.get_units(8);
      float peso_kg_final = peso_g_final / 1000.0f;
      if (peso_kg_final < 0.0f) peso_kg_final = 0.0f;

      float alturaCm = leerAlturaCm();

      Serial.println("Enviando peso+altura a Firestore...");
      bool ok = actualizarBuzonesPorIdentificador(peso_kg_final, alturaCm);

      if (ok) {
        registrarNotificacion(
          identificador,
          "📦 Paquete colocado en " + identificador,
          "Se ha colocado un paquete en el buzón",
          "recibido"
        );

        paquetePresente      = true;
        ultimoPesoEnviadoKg  = peso_kg_final;
        ultimaAlturaEnviada  = alturaCm;
      } else {
        Serial.println("⚠ Error al actualizar Firestore (colocación). No cambiamos estado local.");
      }
    }

    // 2) HABÍA paquete -> AHORA no hay: enviar peso=0 y altura=0
    else if (!hayPaquete && paquetePresente) {
      Serial.println("📤 Paquete retirado. Enviando peso=0 y altura=0...");

      bool ok = actualizarBuzonesPorIdentificador(0.0f, 0.0f);

      if (ok) {
        registrarNotificacion(
          identificador,
          "📤 Paquete retirado en " + identificador,
          "Se ha retirado el paquete actual del buzón",
          "retirado"
        );

        paquetePresente      = false;
        ultimoPesoEnviadoKg  = 0.0f;
        ultimaAlturaEnviada  = 0.0f;
      } else {
        Serial.println("⚠ Error al actualizar Firestore (retirada). No cambiamos estado local -> se reintentará.");
      }
    }

    // 3) Sigue habiendo paquete: actualizar solo peso si cambia bastante
    else if (hayPaquete && paquetePresente) {
      if (fabs(peso_kg - ultimoPesoEnviadoKg) >= DELTA_ENVIO_KG) {
        Serial.println("Cambio de peso con paquete presente -> enviar peso+misma altura.");
        bool ok = actualizarBuzonesPorIdentificador(peso_kg, ultimaAlturaEnviada);
        if (ok) {
          ultimoPesoEnviadoKg = peso_kg;

          /*
          registrarNotificacion(
            identificador,
            "⚖️ Cambio de peso en " + identificador,
            "Se ha detectado un cambio de peso con paquete dentro",
            "movimiento"
          );
          */
        } else {
          Serial.println("⚠ Error al actualizar Firestore (cambio de peso).");
        }
      } else {
        Serial.println("Cambio pequeño -> no se envía.");
      }
    } else {
      Serial.println("Buzón vacío estable -> sin envío.");
    }

    delayRTOS(300); // antes delay(300)
  }
}

// Opcional: tarea “watchdog” de WiFi (NO cambia la lógica principal).
// Solo intenta reconectar si se cae, cada 2s.
void taskWifiWatch(void* pv) {
  (void)pv;
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      conectarWiFi();
    }
    delayRTOS(2000);
  }
}

// ======================================================
// SETUP / LOOP
// ======================================================

void setup() {
  Serial.begin(115200);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  balanza.begin(DT_PIN, SCK_PIN);
  balanza.set_scale(ESCALA);

  Serial.println("Tara inicial (asegúrate de que el buzón está vacío)...");
  balanza.tare(20);
  Serial.println("Tara completa.");

  Serial.print("Conectando WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delayRTOS(200); // antes delay(200)
  }
  Serial.println("\nWiFi Conectado!");
  Serial.println(WiFi.localIP());

  configTime(3600, 0, "pool.ntp.org", "time.nist.gov");
  Serial.println("Obteniendo hora NTP...");
  time_t now = time(nullptr);
  while (now < 100000) {
    delayRTOS(200); // antes delay(200)
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("\nHora NTP obtenida!");

  // Tarea principal: mismo loop de antes, pero en FreeRTOS
  xTaskCreatePinnedToCore(
    taskMainLogic,
    "MainLogic",
    8192,          // stack (HTTP + JSON consume)
    nullptr,
    2,
    nullptr,
    1              // core 1 (suele ir bien con WiFi)
  );

  // Tarea secundaria opcional (puedes comentarla si no la quieres)
  xTaskCreatePinnedToCore(
    taskWifiWatch,
    "WiFiWatch",
    4096,
    nullptr,
    1,
    nullptr,
    0
  );
}

void loop() {
  // Ya no se usa; FreeRTOS ejecuta las tareas.
  vTaskDelay(pdMS_TO_TICKS(1000));
}
