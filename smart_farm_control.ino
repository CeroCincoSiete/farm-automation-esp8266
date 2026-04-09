#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <Servo.h>

// ============================================
//  CONFIGURACIÓN - CAMBIA SÓLO ESTOS DATOS
// ============================================
#define WLAN_SSID     "SSID"
#define WLAN_PASS     "PASSWORD"
#define IO_USERNAME   "USER ADAFRUIT IO"
#define IO_KEY        "KEY ADAFRUIT IO"

// ============================================
//  PINES
// ============================================
#define SERVO_PIN     15
#define DHTPIN        4
#define DHTTYPE       DHT11
#define BOTON_PIN     5

// ============================================
//  OBJETOS
// ============================================
DHT dht(DHTPIN, DHTTYPE);
Servo myServo;
WiFiClient espClient;
PubSubClient mqtt(espClient);

// ============================================
//  VARIABLES
// ============================================
float temperaturaActual = 0;
float limiteTemperatura = 30.0;
int anguloActual = 0;
unsigned long lastTempRead = 0;
const long tempReadInterval = 5000;

// Variables para el botón
bool botonPresionado = false;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 200;

// ============================================
//  PROTOTIPOS
// ============================================
void conectarWiFi();
void conectarMQTT();
void callbackMQTT(char* topic, byte* payload, unsigned int length);
void moverServo(int angulo, String origen);
void publicarMensaje(String texto);
void leerTemperatura();
void leerBoton();

// ============================================
//  SETUP
// ============================================
void setup() {
  Serial.begin(115200);
  delay(10);
  Serial.println("\n--- SMART FARM CONTROL v3 (SIN BUCLES) ---");

  dht.begin();
  myServo.attach(SERVO_PIN);
  myServo.write(0);
  anguloActual = 0;

  pinMode(BOTON_PIN, INPUT_PULLUP);

  conectarWiFi();
  mqtt.setServer("io.adafruit.com", 1883);
  mqtt.setCallback(callbackMQTT);
  mqtt.setBufferSize(1024);
  conectarMQTT();
}

void loop() {
  if (!mqtt.connected()) conectarMQTT();
  mqtt.loop();

  leerTemperatura();
  leerBoton();
}

// ============================================
//  CONEXIONES
// ============================================
void conectarWiFi() {
  Serial.print("Conectando a WiFi");
  WiFi.begin(WLAN_SSID, WLAN_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi conectado! IP: " + WiFi.localIP().toString());
}

void conectarMQTT() {
  while (!mqtt.connected()) {
    Serial.print("Conectando a Adafruit IO...");
    if (mqtt.connect(IO_USERNAME, IO_USERNAME, IO_KEY)) {
      Serial.println("conectado!");
      String topicControl = String(IO_USERNAME) + "/feeds/servo-control";
      mqtt.subscribe(topicControl.c_str());
      Serial.print("Suscrito a: ");
      Serial.println(topicControl);
      publicarMensaje("Sistema iniciado (sin bucles)");
    } else {
      Serial.print("falló, rc=");
      Serial.print(mqtt.state());
      Serial.println(" reintentando en 5 segundos");
      delay(5000);
    }
  }
}

// ============================================
//  CALLBACK MQTT (recibe comandos remotos)
// ============================================
void callbackMQTT(char* topic, byte* payload, unsigned int length) {
  String mensaje = "";
  for (unsigned int i = 0; i < length; i++) mensaje += (char)payload[i];
  Serial.print("Comando remoto recibido: ");
  Serial.println(mensaje);

  int angulo = mensaje.toInt();
  if (angulo >= 0 && angulo <= 180) {
    // El origen es "remoto" -> NO publicaremos de vuelta para evitar bucles
    moverServo(angulo, "remoto");
  } else {
    Serial.println("Comando no válido: " + mensaje);
  }
}

// ============================================
//  FUNCIÓN PRINCIPAL: mover servo y publicar SOLO si es local
// ============================================
void moverServo(int angulo, String origen) {
  // Evita movimientos redundantes
  if (angulo == anguloActual) {
    Serial.println("El servo ya está en " + String(angulo) + "°. No se mueve.");
    return;
  }

  // Mueve el servo físicamente
  myServo.write(angulo);
  anguloActual = angulo;
  Serial.println("Servo movido a " + String(angulo) + "° desde " + origen);

  // Publicar mensaje de texto en el feed estado-manual (siempre, para informar)
  publicarMensaje("Servo movido a " + String(angulo) + "° desde " + origen);

  // **CRUCIAL: Solo publicar el ángulo de vuelta si el origen es LOCAL**
  // Si el origen es "remoto", NO publicamos para evitar el bucle.
  if (origen == "boton" || origen == "automatico") {
    String topicServo = String(IO_USERNAME) + "/feeds/servo-control";
    mqtt.publish(topicServo.c_str(), String(angulo).c_str());
    Serial.println("Ángulo " + String(angulo) + " publicado para sincronizar (origen local)");
  } else {
    Serial.println("Origen remoto: no se publica de vuelta para evitar bucle");
  }
}

// ============================================
//  PUBLICAR MENSAJE EN FEED "estado-manual"
// ============================================
void publicarMensaje(String texto) {
  String topic = String(IO_USERNAME) + "/feeds/estado-manual";
  if (mqtt.publish(topic.c_str(), texto.c_str()))
    Serial.println("Mensaje publicado: " + texto);
  else
    Serial.println("Error al publicar mensaje");
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

      String topicTemp = String(IO_USERNAME) + "/feeds/temperatura";
      mqtt.publish(topicTemp.c_str(), String(temperaturaActual).c_str());

      if (temperaturaActual > limiteTemperatura && anguloActual == 0) {
        Serial.println("Temperatura alta, abriendo riego automático");
        moverServo(90, "automatico");
      }
    } else {
      Serial.println("Error DHT11");
    }
  }
}

// ============================================
//  BOTÓN FÍSICO (alterna entre 0 y 90 grados)
// ============================================
void leerBoton() {
  int lectura = digitalRead(BOTON_PIN);

  if (lectura == LOW && !botonPresionado) {
    if ((millis() - lastDebounceTime) > debounceDelay) {
      botonPresionado = true;
      lastDebounceTime = millis();
      Serial.println("Botón presionado - alternando servo");
      if (anguloActual == 0) {
        moverServo(90, "boton");
      } else {
        moverServo(0, "boton");
      }
    }
  }
  else if (lectura == HIGH && botonPresionado) {
    if ((millis() - lastDebounceTime) > debounceDelay) {
      botonPresionado = false;
      lastDebounceTime = millis();
    }
  }
}
