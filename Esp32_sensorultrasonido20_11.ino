#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h> // Para NTP y timestamps

// -------------------------
// CONFIGURAR WIFI
// -------------------------
const char* ssid = "FabiVD";
const char* password = "Snoopy070506";

// -------------------------
// CONFIGURAR FIRESTORE
// -------------------------
String apiKey     = "AIzaSyBHTeOdFk1BlxFirAU1eFHjonV7YjrlpO4";
String projectID  = "qubi-d3588";
String identificador = "BX001"; // númeroIdentificador a buscar

// -------------------------
// PINES ULTRASONICO HC-SR04
// -------------------------
const int trigPin = 5;
const int echoPin = 16;

// --------------------------
// CONSULTAR BUZONES POR numeroIdentificador Y ACTUALIZAR ALTURA
// --------------------------
void actualizarBuzonesPorIdentificador(float altura) {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  String url = "https://firestore.googleapis.com/v1/projects/" + projectID +
               "/databases/(default)/documents:runQuery?key=" + apiKey;

  http.begin(url);
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
  if (httpResponse <= 0) {
    Serial.println("❌ Error en consulta Firestore");
    http.end();
    return;
  }

  String payload = http.getString();
  http.end();
  Serial.println("📥 Respuesta Query:");
  Serial.println(payload);

  DynamicJsonDocument doc(6000);
  deserializeJson(doc, payload);

  for (JsonVariant v : doc.as<JsonArray>()) {
    if (!v.containsKey("document")) continue;

    String fullName = v["document"]["name"].as<String>();
    int lastSlash = fullName.lastIndexOf('/');
    String docID = fullName.substring(lastSlash + 1);

    Serial.print("📌 Buzón encontrado → ");
    Serial.println(docID);

    HTTPClient http2;
    String urlUpdate = "https://firestore.googleapis.com/v1/projects/" + projectID +
                       "/databases/(default)/documents/buzones/" + docID +
                       "?updateMask.fieldPaths=altura&key=" + apiKey;

    http2.begin(urlUpdate);
    http2.addHeader("Content-Type", "application/json");

    DynamicJsonDocument updateDoc(300);
    JsonObject fields = updateDoc.createNestedObject("fields");
    JsonObject alturaObj = fields.createNestedObject("altura");
    alturaObj["doubleValue"] = altura;

    String updateBody;
    serializeJson(updateDoc, updateBody);

    int updateResponse = http2.PATCH(updateBody);
    Serial.print("📤 PATCH Firestore → Code: ");
    Serial.println(updateResponse);
    if (updateResponse > 0) Serial.println(http2.getString());

    http2.end();
  }
}

// --------------------------
// REGISTRAR NOTIFICACION EN FIRESTORE
// --------------------------
void registrarNotificacion(String buzonId, String titulo, String mensaje) {
  if (WiFi.status() != WL_CONNECTED) return;

  // Obtener fecha y hora actual desde NTP
  time_t now;
  time(&now);
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);

  char buffer[30];
  strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &timeinfo); // Formato Firestore

  HTTPClient http;
  String url = "https://firestore.googleapis.com/v1/projects/" + projectID +
               "/databases/(default)/documents/notificaciones?key=" + apiKey;

  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  DynamicJsonDocument doc(600);
  JsonObject fields = doc.createNestedObject("fields");
  fields["buzonNumeroIdentificador"]["stringValue"] = buzonId;
  fields["mensaje"]["stringValue"] = mensaje;
  fields["titulo"]["stringValue"] = titulo;
  fields["tipo"]["stringValue"] = "movimiento";
  fields["leido"]["booleanValue"] = false;
  fields["usuarioId"]["stringValue"] = "SensorAltura";
  fields["fecha"]["timestampValue"] = String(buffer);

  String payload;
  serializeJson(doc, payload);

  int code = http.POST(payload);
  Serial.print("📤 POST Firestore Notificación → Code: ");
  Serial.println(code);
  if (code > 0) Serial.println(http.getString());

  http.end();
}

// --------------------------
// SETUP
// --------------------------
void setup() {
  Serial.begin(115200);
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  // Conectar WiFi
  Serial.print("🔌 Conectando WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(400);
  }
  Serial.println("\n📶 WiFi Conectado!");
  Serial.println(WiFi.localIP());

  // Configurar NTP (GMT+1)
  configTime(3600, 0, "pool.ntp.org", "time.nist.gov");
  Serial.println("⏱ Obteniendo hora NTP...");
  time_t now = time(nullptr);
  while (now < 100000) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("\n✅ Hora NTP obtenida!");
}

// --------------------------
// LOOP
// --------------------------
bool paqueteDetectado = false;

void loop() {
  // Medición ultrasónica
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  long duration = pulseIn(echoPin, HIGH);
  float distance = duration * 0.034 / 2;
  float altura = 58.45 - distance;

  Serial.print("📏 Altura medida: ");
  Serial.println(altura);

  if (altura <= 1.0) {  // Paquete retirado
    if (paqueteDetectado) {
      Serial.println("📦 Paquete retirado → Enviando altura 0");
      actualizarBuzonesPorIdentificador(0);

      // Notificación de paquete retirado
      registrarNotificacion(
        identificador,
        "📤 Paquete retirado en " + identificador,
        "Se ha retirado el paquete actual"
      );

      paqueteDetectado = false;
    }
  } else { // Paquete colocado
    if (!paqueteDetectado) {
      Serial.println("⏳ Paquete detectado, esperando 2.5s para estabilizar...");
      delay(2500);

      // Segunda lectura
      digitalWrite(trigPin, LOW);
      delayMicroseconds(2);
      digitalWrite(trigPin, HIGH);
      delayMicroseconds(10);
      digitalWrite(trigPin, LOW);

      duration = pulseIn(echoPin, HIGH);
      distance = duration * 0.034 / 2;
      altura = 58.45 - distance;

      Serial.print("📏 Altura final estabilizada: ");
      Serial.println(altura);

      if (altura > 1.0) {
        Serial.println("📤 Enviando altura final a Firestore...");
        actualizarBuzonesPorIdentificador(altura);

        // Notificación de paquete colocado
        registrarNotificacion(
          identificador,
          "📦 Paquete colocado en " + identificador,
          "Se ha colocado un paquete en el buzón"
        );

        paqueteDetectado = true;
      } else {
        Serial.println("⚠ Tras esperar, la altura volvió a ser ≤ 1 cm, no se envía nada.");
      }
    }
  }

  delay(1000); // Loop cada 1s
}