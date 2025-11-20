#include <M5Stack.h>
#include <WiFi.h>
#include <PubSubClient.h>

// --- Configuración WiFi ---
const char* ssid = "DIGIFIBRA-P9zt";
const char* password = "4t5Z7y3YutEC";

// --- Configuración MQTT ---
const char* mqtt_server = "192.168.1.155";
const int mqtt_port = 1883;
const char* mqtt_topic = "qubi/datosraspberry";

WiFiClient espClient;
PubSubClient client(espClient);

#define SW420_PIN 36
#define LED_PIN   26

bool sensorActivo = true;

// Variables para temporizadores
unsigned long lastLedTime = 0;
unsigned long ledDuration = 300; // ms

unsigned long lastDebounce = 0;
unsigned long debounceDelay = 200; // ms para el botón

bool ledState = false;

void setup_wifi() {
  Serial.println("Conectando WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi conectado!");
}

void reconnect() {
  while (!client.connected()) {
    Serial.println("Intentando conectar MQTT...");
    String clientId = "M5Stack_" + String(random(0xffff), HEX);
    if (client.connect(clientId.c_str())) {
      Serial.println("Conectado a Mosquitto!");
    } else {
      Serial.print("Fallo MQTT rc=");
      Serial.println(client.state());
      delay(2000);
    }
  }
}

  void mostrarEstadoSensor() {
  M5.Lcd.fillScreen(BLACK);

  // TÍTULO FIJO
  M5.Lcd.setCursor(10, 40);
  M5.Lcd.setTextSize(3);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.println("SIMULACION DE");
  M5.Lcd.setCursor(10, 90);
  M5.Lcd.println("CERRADURA");

  // ESTADO DEL SENSOR
  M5.Lcd.setCursor(10, 150);
  M5.Lcd.setTextSize(2);
  

  if (sensorActivo) {
      M5.Lcd.setTextColor(GREEN);
    M5.Lcd.println("Puerta CERRADA");
    M5.Lcd.setCursor(10, 190);
    M5.Lcd.println("(sensor activado)");
  } else {
    M5.Lcd.setTextColor(RED);
    M5.Lcd.println("Puerta ABIERTA");
    M5.Lcd.setCursor(10, 190);
    M5.Lcd.println("(sensor desactivado)");
  }
}


void setup() {
  M5.begin();
  Serial.begin(115200);

  pinMode(SW420_PIN, INPUT_PULLDOWN); // Pull-down para estabilidad
  pinMode(LED_PIN, OUTPUT);

  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);

  mostrarEstadoSensor(); // Estado inicial en pantalla
}

void loop() {
  M5.update();

  if (!client.connected()) reconnect();
  client.loop();

  // Activar/desactivar sensor con botón B (con debounce)
  if (M5.BtnB.wasPressed()) {
    unsigned long now = millis();
    if (now - lastDebounce > debounceDelay) {
      sensorActivo = !sensorActivo;
      mostrarEstadoSensor();
      lastDebounce = now;
    }
  }

  // Lectura del SW420 solo si el sensor está activo
  if (sensorActivo) {
    int lectura = digitalRead(SW420_PIN);
    if (lectura == HIGH && !ledState) {
      Serial.println("Movimiento detectado!");
      client.publish(mqtt_topic, "ALERTA: INTENTO DE ROBO");
      digitalWrite(LED_PIN, HIGH);
      ledState = true;
      lastLedTime = millis();
    }
  }

  // Apagar LED automáticamente sin delay
  if (ledState && millis() - lastLedTime >= ledDuration) {
    digitalWrite(LED_PIN, LOW);
    ledState = false;
  }
}