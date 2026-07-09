#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>

// PINES ESP32
const int PIN_GAS = 34;    
const int PIN_TEMP = 32;   // Aquí va el cable de DATOS del DHT22
const int PIN_SIRENA = 25; 
const int PIN_RELE = 26;   
const int PIN_ROJO = 27;   
const int PIN_VERDE = 14;  

// CONFIGURACIÓN DHT22
#define DHTTYPE DHT22
DHT dht(PIN_TEMP, DHTTYPE);

// UMBRALES Y CONSTANTES
const int UMBRAL_GAS_PPM = 1000; 
const int UMBRAL_TEMP = 70;

// ¡PON TU WIFI REAL AQUÍ!
const char* ssid = "TU_WIFI_AQUI";
const char* password = "TU_CLAVE_AQUI";
const char* mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;

// MQTT TOPICS
const char* topic_gas = "ubo/g5/sensor/gas";
const char* topic_temp = "ubo/g5/sensor/temperatura";
const char* topic_estado = "ubo/g5/sensor/estado";
const char* topic_control = "ubo/g5/control";

WiFiClient espClient;
PubSubClient client(espClient);
LiquidCrystal_I2C lcd(0x27, 16, 2);

unsigned long previousMillisSensors = 0;
const long intervalSensors = 2000; 
unsigned long lastReconnectAttempt = 0;
bool comandoEmergenciaManual = false;

// DECLARACIÓN DE FUNCIONES
float calcularPPM(int lectura);
void setup_wifi();
boolean reconnectMQTT();
void callback(char* topic, byte* payload, unsigned int length);

void setup() {
  Serial.begin(115200);

  pinMode(PIN_SIRENA, OUTPUT);
  pinMode(PIN_RELE, OUTPUT);
  pinMode(PIN_ROJO, OUTPUT);
  pinMode(PIN_VERDE, OUTPUT);
  
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0,0);
  lcd.print("Iniciando...");

  dht.begin(); // <-- Iniciamos el DHT22

  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  
  lcd.clear();
}

void loop() {
  unsigned long currentMillis = millis();

  // GESTIÓN DE CONEXIÓN NO BLOQUEANTE
  if (!client.connected()) {
    if (currentMillis - lastReconnectAttempt >= 5000) {
      lastReconnectAttempt = currentMillis;
      if (reconnectMQTT()) {
        lastReconnectAttempt = 0;
      }
    }
  } else {
    client.loop(); 
  }

  // LECTURA DE SENSORES Y LÓGICA LOCAL
  if (currentMillis - previousMillisSensors >= intervalSensors) {
    previousMillisSensors = currentMillis;

    // LECTURA GAS
    int lecturaRawGas = analogRead(PIN_GAS);
    float ppm = calcularPPM(lecturaRawGas);
    
    // LECTURA TEMPERATURA DHT22
    float temperaturaC = dht.readTemperature();
    
    // Si el DHT22 falla o se desconecta el cable, devuelve NaN (Not a Number)
    if (isnan(temperaturaC)) {
      Serial.println("Error leyendo el DHT22, revisa los cables!");
      temperaturaC = 0; // Valor seguro temporal en caso de error
    }

    // Impresión en consola
    Serial.print("RAW Gas: "); Serial.print(lecturaRawGas);
    Serial.print(" | PPM: "); Serial.print(ppm);
    Serial.print(" | Temp: "); Serial.println(temperaturaC);

    // ACTUALIZAR PANTALLA
    lcd.setCursor(0, 0);
    lcd.print("Gas:"); lcd.print((int)ppm); lcd.print("ppm   "); 
    lcd.setCursor(0, 1);
    lcd.print("T:"); lcd.print((int)temperaturaC); lcd.print("C     ");

    // PUBLICAR EN MQTT
    if (client.connected()) {
      client.publish(topic_gas, String(ppm).c_str());
      client.publish(topic_temp, String(temperaturaC).c_str());
    }

    // LÓGICA DE CONTROL LOCAL Y ESTADOS
    if (ppm > UMBRAL_GAS_PPM || temperaturaC > UMBRAL_TEMP || comandoEmergenciaManual) {
      digitalWrite(PIN_RELE, HIGH);
      digitalWrite(PIN_ROJO, HIGH);
      digitalWrite(PIN_VERDE, LOW);
      digitalWrite(PIN_SIRENA, HIGH); 
      
      if (client.connected()) client.publish(topic_estado, "PELIGRO");
    } else {
      digitalWrite(PIN_RELE, LOW);
      digitalWrite(PIN_ROJO, LOW);
      digitalWrite(PIN_VERDE, HIGH);
      digitalWrite(PIN_SIRENA, LOW); 
      
      if (client.connected()) client.publish(topic_estado, "SEGURO");
    }
  }
}

// FUNCIÓN GAS SENSOR
float calcularPPM(int lectura) {
  if (lectura <= 843) return 0.1;
  else if (lectura <= 3768) return (lectura - 843.0) * (1047.0 - 0.1) / (3768.0 - 843.0) + 0.1;
  else return (lectura - 3768.0) * (100000.0 - 1047.0) / (4041.0 - 3768.0) + 1047.0;
}

// FUNCIONES DE RED Y MQTT
void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Conectando a ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi conectado");
}

boolean reconnectMQTT() {
  Serial.print("Intentando conexión MQTT...");
  String clientId = "ESP32Client-ZeroFlame";
  clientId += String(random(0xffff), HEX);
  
  if (client.connect(clientId.c_str())) {
    Serial.println("conectado");
    client.subscribe(topic_control);
    return true;
  } else {
    Serial.print("falló, rc=");
    Serial.print(client.state());
    Serial.println(" (Reintento en 5s)");
    return false;
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  if (message == "TEST") {
    comandoEmergenciaManual = true;
    Serial.println("COMANDO: Activando modo prueba (PELIGRO)");
  } else if (message == "RESET") {
    comandoEmergenciaManual = false;
    Serial.println("COMANDO: Reseteando al modo normal");
  }
}