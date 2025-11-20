#include <WiFi.h>
#include <WiFiUdp.h>
#include <M5Stack.h>

// -------------------------
// CONFIG WIFI
// -------------------------
const char* ssid = "DIGIFIBRA-P9zt";
const char* password = "4t5Z7y3YutEC";

const char* esp32cam_ip = "192.168.1.151"; 
const int udpPort = 4210;

WiFiUDP Udp;

// Variables para controlar rebote y envío único
bool botonPresionado = false;
unsigned long ultimoTiempoBoton = 0;
const unsigned long debounceTime = 200; // ms

void enviarUDP(const char* mensaje) {
  Udp.beginPacket(esp32cam_ip, udpPort);
  Udp.print(mensaje);
  Udp.endPacket();

  Serial.print("📤 Enviado: ");
  Serial.println(mensaje);
}

// -------------------------
// FUNCIONES PANTALLA
// -------------------------
void mostrarPantallaInicio() {
  M5.Lcd.clear();
  M5.Lcd.setTextSize(3);
  M5.Lcd.setTextColor(WHITE, BLACK);
  M5.Lcd.setCursor(20, 100);
  M5.Lcd.println("PULSE EL BOTON B");
}

void setup() {
  M5.begin();
  Serial.begin(115200);

  M5.Lcd.setTextSize(2);
  M5.Lcd.print("Conectando WiFi...");
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    M5.Lcd.print(".");
  }

  M5.Lcd.println("\nWiFi conectado!");
  M5.Lcd.print("IP: ");
  M5.Lcd.println(WiFi.localIP());

  delay(1200);
  mostrarPantallaInicio();   // ⬅️ Mostramos pantalla una sola vez
}

void loop() {
  M5.update();

  bool estadoActual = M5.BtnB.isPressed();
  unsigned long ahora = millis();

  // Detectar pulsación del botón A (solo cuando cambia de estado)
  if (estadoActual && !botonPresionado && (ahora - ultimoTiempoBoton > debounceTime)) {

    botonPresionado = true;
    ultimoTiempoBoton = ahora;

    // Mostrar mensaje grande
    M5.Lcd.clear();
    M5.Lcd.setTextSize(3);
    M5.Lcd.setTextColor(GREEN, BLACK);

    M5.Lcd.setCursor(10, 40);
    M5.Lcd.println("LE VAMOS A");

    M5.Lcd.setCursor(10, 80);
    M5.Lcd.println("TOMAR UNA");

    M5.Lcd.setCursor(10, 120);
    M5.Lcd.println("FOTO");

    M5.Lcd.setCursor(10, 170);
    M5.Lcd.println("ESPERE UN");

    M5.Lcd.setCursor(10, 210);
    M5.Lcd.println("POCO...");

    // Enviar a la cámara
    enviarUDP("HIGH");
    delay(1000);
    enviarUDP("LOW");

    // Regresar a la pantalla de inicio después de un segundo
    delay(1200);
    mostrarPantallaInicio();  
  }

  // Cuando se suelta el botón permitir otra pulsación
  if (!estadoActual && botonPresionado && (ahora - ultimoTiempoBoton > debounceTime)) {
    botonPresionado = false;
    ultimoTiempoBoton = ahora;
  }
}
