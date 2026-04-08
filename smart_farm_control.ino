/*
 * Smart Farm Control - Riego y Dispensador Inteligente
 * 
 * Control remoto vía Adafruit IO, botón físico y automatización por temperatura.
 * 
 * Autor: Tu Nombre
 * Proyecto para Hackatón
 * Licencia: MIT
 * 
 * Componentes:
 * - ESP8266 (NodeMCU)
 * - Servomotor SG90/MG995
 * - Sensor DHT11
 * - Pulsador
 */

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <Servo.h>

// ============================================
//  CONFIGURACIÓN - ¡CAMBIA ESTOS VALORES!
// ============================================

// --- WiFi ---
#define WLAN_SSID     "nombre_de_tu_wifi"     // <--- CAMBIA AQUÍ
#define WLAN_PASS     "contraseña_de_tu_wifi" // <--- CAMBIA AQUÍ

// --- Adafruit IO ---
#define IO_USERNAME   "tu_usuario_adafruit"   // <--- CAMBIA AQUÍ
#define IO_KEY        "tu_aio_key"            // <--- CAMBIA AQUÍ

// ============================================
//  CONFIGURACIÓN DE PINES
// ============================================
#define SERVO_PIN     15     // GPIO15 (D8 en NodeMCU)
#define DHTPIN        4      // GPIO4  (D2 en NodeMCU)
#define DHTTYPE       DHT11
#define BOTON_PIN     5      // GPIO5  (D1 en NodeMCU) - botón a GND

// ============================================
//  OBJETOS
// ============================================
DHT dht(DHTPIN, DHTTYPE);
Servo myServo;
WiFiClient espClient;
PubSubClient mqtt(espClient);

// ============================================
//  VARIABLES DE CONTROL
// ============================================
float temperaturaActual = 0;
float limiteTemperatura = 30.0;   // Riego automático si supera 30°C
bool servoCerrado = true;          // true=cerrado (0°), false=abierto (90°)
unsigned long lastTempRead = 0;
const long tempReadInterval = 5000; // Leer temperatura cada 5 segundos

// Variables para el botón (debounce)
int lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;

// ============================================
//  PROTOTIPOS DE FUNCIONES
// ============================================
void connectWiFi();
void connectMQTT();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void abrirRiego(String origen);
void cerrarRiego(String origen);
void publicarMensaje(String texto);
void leerTemperatura();
void leerBoton();
void handleServoCommand(String cmd);

// ============================================
//  SETUP
// ============================================
void setup() {
  Serial.begin(115200);
  delay(10);

  dht.begin();
  myServo.attach(SERVO_PIN);
  myServo.write(0);          // Inicia cerrado
  servoCerrado = true;

  pinMode(BOTON_PIN, INPUT_PULLUP);

  connectWiFi();
  
  mqtt.setServer("io.adafruit.com", 1883);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(1024);
  connectMQTT();
}

// ============================================
//  LOOP PRINCIPAL
// ============================================
void loop() {
  if (!mqtt.connected()) {
    connectMQTT();
  }
  mqtt.loop();

  leerTemperatura();
  leerBoton();
}

// ============================================
//  CONEXIONES
// ============================================
void connectWiFi() {
  Serial.print("Conectando a WiFi");
  WiFi.begin(WLAN_SSID, WLAN_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi conectado! IP: " + WiFi.localIP().toString());
}

void connectMQTT() {
  while (!mqtt.connected()) {
    Serial.print("Conectando a Adafruit IO...");
    
    // Método connect con parámetros "will" (necesario para Adafruit)
    if (mqtt.connect(IO_USERNAME, IO_USERNAME, IO_KEY, "", 0, 0, "")) {
      Serial.println("conectado!");
      
      // Suscripción al feed de control
      String topicControl = String(IO_USERNAME) + "/feeds/servo-control";
      mqtt.subscribe(topicControl.c_str());
      Serial.print("Suscrito a: ");
      Serial.println(topicControl);
      
      // Publicar mensaje de inicio
      publicarMensaje("Sistema iniciado correctamente");
      
    } else {
      Serial.print("falló, rc=");
      Serial.print(mqtt.state());
      Serial.println(" reintentando en 5 segundos");
      delay(5000);
    }
  }
}

// ============================================
//  CALLBACK MQTT (cuando llega un mensaje)
// ============================================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String mensaje = "";
  for (unsigned int i = 0; i < length; i++) {
    mensaje += (char)payload[i];
  }
  Serial.print("Mensaje recibido en [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(mensaje);
  
  handleServoCommand(mensaje);
}

// ============================================
//  CONTROL DEL SERVO (ABRIR/CERRAR)
// ============================================
void abrirRiego(String origen) {
  if (servoCerrado) {
    myServo.write(90);
    servoCerrado = false;
    Serial.println("RIEGO ABIERTO (servo a 90°)");
    
    String mensaje;
    if (origen == "automatico") {
      mensaje = "Riego automático por temperatura (" + String(temperaturaActual) + "°C)";
    } else {
      mensaje = "Riego activado desde " + origen;
    }
    publicarMensaje(mensaje);
  } else {
    Serial.println("El riego ya estaba abierto, no se hace nada");
  }
}

void cerrarRiego(String origen) {
  if (!servoCerrado) {
    myServo.write(0);
    servoCerrado = true;
    Serial.println("RIEGO CERRADO (servo a 0°)");
    
    String mensaje = "Riego desactivado desde " + origen;
    publicarMensaje(mensaje);
  } else {
    Serial.println("El riego ya estaba cerrado, no se hace nada");
  }
}

void handleServoCommand(String cmd) {
  cmd.toLowerCase();
  if (cmd == "on" || cmd == "1") {
    abrirRiego("remoto");
  } 
  else if (cmd == "off" || cmd == "0") {
    cerrarRiego("remoto");
  }
  else {
    // Si por error recibe un ángulo (ej. 90) lo interpreta
    int angulo = cmd.toInt();
    if (angulo >= 0 && angulo <= 180) {
      myServo.write(angulo);
      servoCerrado = (angulo == 0);
      Serial.print("Servo movido a " + String(angulo) + " grados");
      publicarMensaje("Servo movido a " + String(angulo) + "° desde comando");
    } else {
      Serial.println("Comando no reconocido: " + cmd);
    }
  }
}

// ============================================
//  PUBLICAR MENSAJE EN FEED "estado-manual"
// ============================================
void publicarMensaje(String texto) {
  String topic = String(IO_USERNAME) + "/feeds/estado-manual";
  if (mqtt.publish(topic.c_str(), texto.c_str())) {
    Serial.println("Mensaje publicado: " + texto);
  } else {
    Serial.println("Error al publicar mensaje");
  }
}

// ============================================
//  LECTURA DE TEMPERATURA Y AUTOMATIZACIÓN
// ============================================
void leerTemperatura() {
  if (millis() - lastTempRead >= tempReadInterval) {
    lastTempRead = millis();
    float t = dht.readTemperature();
    if (!isnan(t)) {
      temperaturaActual = t;
      Serial.print("Temperatura: ");
      Serial.print(temperaturaActual);
      Serial.println(" °C");
      
      // Publicar temperatura en Adafruit IO
      String topic = String(IO_USERNAME) + "/feeds/temperatura";
      mqtt.publish(topic.c_str(), String(temperaturaActual).c_str());
      
      // Automatización por temperatura: si supera el límite Y el servo está cerrado, lo abre
      if (temperaturaActual > limiteTemperatura && servoCerrado) {
        Serial.println("Temperatura alta -> abriendo riego automáticamente");
        abrirRiego("automatico");
      }
    } else {
      Serial.println("Error leyendo el sensor DHT11");
    }
  }
}

// ============================================
//  LECTURA DEL BOTÓN FÍSICO (TOGGLE)
// ============================================
void leerBoton() {
  int reading = digitalRead(BOTON_PIN);
  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }
  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading == LOW && lastButtonState == HIGH) {
      Serial.println("Botón físico presionado -> alternando estado");
      if (servoCerrado) {
        abrirRiego("manual");
      } else {
        cerrarRiego("manual");
      }
    }
  }
  lastButtonState = reading;
}
