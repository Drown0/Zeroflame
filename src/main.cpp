#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <LiquidCrystal_I2C.h>

// --- PINES ESP32 ---
const int PIN_GAS = 34;    
const int PIN_TEMP = 35;   
const int PIN_SIRENA = 25; 
const int PIN_RELE = 26;   
const int PIN_ROJO = 27;   
const int PIN_VERDE = 14;  

// --- UMBRALES Y CONSTANTES ---
const int UMBRAL_GAS_PPM = 1000; 
const int UMBRAL_TEMP = 70;
const float BETA = 3950; 

// --- CONFIGURACIÓN MQTT Y WIFI ---
const char* ssid = "Wokwi-GUEST";
const char* password = "";
const char* mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;

// --- TÓPICOS MQTT 
const char* topic_gas = "ubo/g5/sensor/gas";
const char* topic_temp = "ubo/g5/sensor/temperatura";
const char* topic_estado = "ubo/g5/sensor/estado";
const char* topic_control = "ubo/g5/control";

WiFiClient espClient;
PubSubClient client(espClient);
LiquidCrystal_I2C lcd(0x27, 16, 2);

unsigned long previousMillisSensors = 0;
const long intervalSensors = 2000; 
bool comandoEmergenciaManual = false;

// --- DECLARACIÓN DE FUNCIONES ---
float calcularPPM(int lectura);
void setup_wifi();
void reconnect();
void callback(char* topic, byte* payload, unsigned int length);

void setup() {
  Serial.begin(115200);

  // Volvemos al pinMode simple
  pinMode(PIN_SIRENA, OUTPUT);
  pinMode(PIN_RELE, OUTPUT);
  pinMode(PIN_ROJO, OUTPUT);
  pinMode(PIN_VERDE, OUTPUT);
  
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0,0);
  lcd.print("Iniciando...");

  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  
  lcd.clear();
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop(); 

  unsigned long currentMillis = millis();

  if (currentMillis - previousMillisSensors >= intervalSensors) {
    previousMillisSensors = currentMillis;

    // 1. LECTURAS
    int lecturaRawGas = analogRead(PIN_GAS);
    float ppm = calcularPPM(lecturaRawGas);
    
    int lecturaTemp = analogRead(PIN_TEMP);
    float temperaturaC = 25.0; 
    if(lecturaTemp > 0 && lecturaTemp < 4095) {
      temperaturaC = 1 / (log(1 / (4095. / lecturaTemp - 1)) / BETA + 1.0 / 298.15) - 273.15;
    }

    // Impresión limpia en consola con RAW incluido
    Serial.print("RAW Gas: "); Serial.print(lecturaRawGas);
    Serial.print(" | PPM: "); Serial.print(ppm);
    Serial.print(" | Temp: "); Serial.println(temperaturaC);

    // 2. ACTUALIZAR PANTALLA
    lcd.setCursor(0, 0);
    lcd.print("Gas:"); lcd.print((int)ppm); lcd.print("ppm   "); 
    lcd.setCursor(0, 1);
    lcd.print("T:"); lcd.print((int)temperaturaC); lcd.print("C     ");

    // 3. PUBLICAR EN MQTT
    client.publish(topic_gas, String(ppm).c_str());
    client.publish(topic_temp, String(temperaturaC).c_str());

    // 4. LÓGICA DE CONTROL LOCAL Y ESTADOS (Simple y estable)
    if (ppm > UMBRAL_GAS_PPM || temperaturaC > UMBRAL_TEMP || comandoEmergenciaManual) {
      digitalWrite(PIN_RELE, HIGH);
      digitalWrite(PIN_ROJO, HIGH);
      digitalWrite(PIN_VERDE, LOW);
      digitalWrite(PIN_SIRENA, HIGH); 
      
      client.publish(topic_estado, "PELIGRO");
      Serial.println("ESTADO: PELIGRO");
    } else {
      digitalWrite(PIN_RELE, LOW);
      digitalWrite(PIN_ROJO, LOW);
      digitalWrite(PIN_VERDE, HIGH);
      digitalWrite(PIN_SIRENA, LOW); 
      
      client.publish(topic_estado, "SEGURO");
      Serial.println("ESTADO: SEGURO");
    }
    Serial.println("-------------------------");
  }
}

// --- FUNCIÓN CALIBRADA EXACTA PARA EL WOKWI GAS SENSOR ---
float calcularPPM(int lectura) {
  if (lectura <= 843) {
    return 0.1;
  } else if (lectura <= 3768) {
    return (lectura - 843.0) * (1047.0 - 0.1) / (3768.0 - 843.0) + 0.1;
  } else {
    return (lectura - 3768.0) * (100000.0 - 1047.0) / (4041.0 - 3768.0) + 1047.0;
  }
}

// --- FUNCIONES DE RED Y MQTT ---
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

void reconnect() {
  while (!client.connected()) {
    Serial.print("Intentando conexión MQTT...");
    String clientId = "ESP32Client-ZeroFlame";
    clientId += String(random(0xffff), HEX);
    
    if (client.connect(clientId.c_str())) {
      Serial.println("conectado");
      client.subscribe(topic_control);
    } else {
      Serial.print("falló, rc=");
      Serial.print(client.state());
      Serial.println(" intentando en 5 segundos");
      delay(5000);
    }
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.print("Mensaje recibido en [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(message);

  if (message == "TEST") {
    comandoEmergenciaManual = true;
    Serial.println("COMANDO: Activando modo prueba (PELIGRO)");
  } else if (message == "RESET") {
    comandoEmergenciaManual = false;
    Serial.println("COMANDO: Reseteando al modo normal");
  }
}