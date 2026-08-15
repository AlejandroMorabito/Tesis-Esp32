#include <DNSServer.h>
#include <Preferences.h>
#include <Bounce2.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

Preferences preferences;
const byte DNS_PORT = 53;
DNSServer dnsServer;
WebServer server(80);

String saved_ssid = "";
String saved_password = "";
String current_ssid = "SSID_guardado_actualmente";

bool wifiConnection = false;
unsigned long lastReconnectAttempt = 0;
const long RECONNECT_INTERVAL = 30000;

String chipID_saved = "";
String firebaseHost = "";
String firebaseAuthToken = "";  // Aún útil para futuro
String firebaseBasePath = "/dispositivos/";

const int LEDmode = 12;
const int modeButton = 13;
const int armButton = 14;
const int C1 = 15;
const int C2 = 16;
const int C3 = 17;
const int C4 = 18;
const int C5 = 19;
const int LEDalarm = 25;
const int LEDarm = 26;
const int alarmButton = 27;

const long ALARM_DURATION = 60000;
unsigned long previousMillis = 0;
unsigned long alarmStartTime = 0;
int ledState = LOW;

unsigned long modePreviousMillis = 0;
int modeLedState = LOW;
long modeBlinkInterval = 0;

String Mode = "Operation";

bool systemArmed = true;

bool alarmActive = false;
bool anyIntrusion = false;
bool anyIntrusion1 = false;
bool anyIntrusion2 = false;
bool anyIntrusion3 = false;
bool anyIntrusion4 = false;
bool anyIntrusion5 = false;
bool anyIntrusion6 = false;
bool alarmDataBase = false;

Bounce2::Button bounceC1 = Bounce2::Button();
Bounce2::Button bounceC2 = Bounce2::Button();
Bounce2::Button bounceC3 = Bounce2::Button();
Bounce2::Button bounceC4 = Bounce2::Button();
Bounce2::Button bounceC5 = Bounce2::Button();
Bounce2::Button bounceArm = Bounce2::Button();
Bounce2::Button bounceAlarm = Bounce2::Button();
Bounce2::Button bounceMode = Bounce2::Button();

// Declaraciones de funciones
bool sendToFirebaseREST(const String& path, const String& data);
bool addToQueue(const String& path, bool value);
void processQueue();
bool sendEvent(const String& path, bool value);
void checkFirebaseCommands();
void initFirebase();
void initializeSystem();
void testFirebaseREST();
void registerDeviceFirebaseREST();
void loadFactoryConfig();
void saveFactoryConfig();
bool connectToWiFi();
void startAccessPoint();
bool isModeButtonPressed();
void handleRoot();
void handleFactory();
void handleConnect();
void handleFactoryReset();
void handleSave();
int scanNetworks();
void blinkModeLED();
void processAlarmLogic();
void resetAlarmState();
void printSystemStatus();
void verifyFirebaseConfig();
bool isFirebaseConnected();
unsigned long getUnixTimestamp();
void syncNTP();
void updateDeviceState();
void createFirebaseEvent(const String& eventType, const String& message, bool value = true);

#define MAX_QUEUE_SIZE 50

struct Event {
  String path;
  bool value;
  unsigned long timestamp;
};

Event eventQueue[MAX_QUEUE_SIZE];
int queueStart = 0;
int queueEnd = 0;
int queueSize = 0;

String firebaseURL = "";  // URL base de Firebase

// ==================== FUNCIONES PARA TIMESTAMP UNIX ====================

unsigned long getUnixTimestamp() {
  unsigned long currentTime = 0;

  // Configurar NTP (Network Time Protocol) si estamos conectados a WiFi
  if (WiFi.status() == WL_CONNECTED) {
    configTime(-5 * 3600, 0, "pool.ntp.org", "time.nist.gov");  // UTC-5 para Lima

    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      time_t epochTime = mktime(&timeinfo);
      currentTime = (unsigned long)epochTime * 1000;
      currentTime += (millis() % 1000);  // Agregar milisegundos actuales
    } else {
      currentTime = millis() + (24 * 3600000);
    }
  } else {
    currentTime = millis() + (30 * 24 * 3600000);
  }

  return currentTime;
}

void syncNTP() {
  if (WiFi.status() == WL_CONNECTED) {
    configTime(-5 * 3600, 0, "pool.ntp.org", "time.nist.gov");
    delay(1000);

    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 10000)) {
      char buffer[30];
      strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
    }
  }
}

// ==================== FIREBASE REST API FUNCTIONS ====================
void initFirebase() {
  // Construir URL base
  if (firebaseHost.length() == 0) {
    Serial.println("❌ Firebase Host no configurado");
    return;
  }

  firebaseURL = "https://" + firebaseHost;

  // Probar conexión
  testFirebaseREST();
}

bool sendToFirebaseREST(const String& path, const String& data) {
  if (firebaseURL.length() == 0 || chipID_saved.length() == 0) {
    return false;
  }

  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  HTTPClient http;
  String fullURL = firebaseURL + path + ".json";

  http.begin(fullURL);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(10000);

  int httpCode = http.PUT(data);
  http.end();

  bool success = (httpCode == 200);

  return success;
}

void testFirebaseREST() {
  unsigned long currentTime = getUnixTimestamp();
  String testPath = "/connection/"+chipID_saved+"/test_connection_" + String(currentTime);

  DynamicJsonDocument doc(256);
  doc["device_id"] = chipID_saved;
  doc["test"] = "connection_test";
  doc["timestamp"] = currentTime;
  doc["rssi"] = WiFi.RSSI();
  doc["ip"] = WiFi.localIP().toString();

  String jsonStr;
  serializeJson(doc, jsonStr);

  if (sendToFirebaseREST(testPath, jsonStr)) {
    Serial.println("✅ Firebase: Conexión exitosa");
    syncNTP();
    registerDeviceFirebaseREST();
  } else {
    Serial.println("❌ Firebase: Error de conexión");
  }
}

void registerDeviceFirebaseREST() {
  unsigned long currentTime = getUnixTimestamp();

  DynamicJsonDocument doc(1024);

  // Datos principales
  doc["device_id"] = chipID_saved;
  doc["name"] = "Alarma de Cerco " + chipID_saved;
  doc["type"] = "security_alarm";
  doc["status"] = "online";
  doc["ip_address"] = WiFi.localIP().toString();
  doc["mac_address"] = WiFi.macAddress();
  doc["rssi"] = WiFi.RSSI();
  doc["firmware_version"] = "1.0";
  doc["last_seen"] = currentTime;
  doc["created_at"] = currentTime;

  // Configuración
  JsonObject config = doc.createNestedObject("config");
  config["armed"] = false;
  config["alarm_delay"] = 30;
  config["sensitivity"] = "medium";
  config["auto_arm"] = false;

  // Estado
  JsonObject state = doc.createNestedObject("state");
  state["system_armed"] = systemArmed;
  state["alarm_active"] = false;
  state["any_intrusion"] = false;

  // Sensores
  JsonObject sensors = doc.createNestedObject("sensors");
  sensors["c1"] = anyIntrusion1;
  sensors["c2"] = anyIntrusion2;
  sensors["c3"] = anyIntrusion3;
  sensors["c4"] = anyIntrusion4;
  sensors["c5"] = anyIntrusion5;

  String jsonStr;
  serializeJson(doc, jsonStr);

  String path = "/devices/" + chipID_saved;

  if (sendToFirebaseREST(path, jsonStr)) {
    Serial.println("✅ Firebase: Dispositivo registrado");
  } else {
    Serial.println("⚠️  Firebase: Error registrando dispositivo");
  }
}

void createFirebaseEvent(const String& eventType, const String& message, bool value) {
  unsigned long timestamp = getUnixTimestamp();
  String eventPath = "/alarm_events/" + chipID_saved + "/" + String(timestamp);

  DynamicJsonDocument doc(512);
  doc["device_id"] = chipID_saved;
  doc["event_type"] = eventType;
  doc["event_value"] = value;
  doc["message"] = message;
  doc["timestamp"] = timestamp;
  doc["created_at"] = timestamp;

  String jsonStr;
  serializeJson(doc, jsonStr);

  if (sendToFirebaseREST(eventPath, jsonStr)) {
    Serial.print("📤 Firebase Event: ");
    Serial.print(eventType);
    Serial.print(" = ");
    Serial.print(value ? "true" : "false");
    Serial.print(" (");
    Serial.print(message);
    Serial.println(")");
  }
}

// ==================== QUEUE & EVENT FUNCTIONS ====================

bool addToQueue(const String& path, bool value) {
  if (queueSize >= MAX_QUEUE_SIZE) {
    queueStart = (queueStart + 1) % MAX_QUEUE_SIZE;
    queueSize--;
  }

  eventQueue[queueEnd].path = path;
  eventQueue[queueEnd].value = value;
  eventQueue[queueEnd].timestamp = millis();

  queueEnd = (queueEnd + 1) % MAX_QUEUE_SIZE;
  queueSize++;

  Serial.print("📥 Encolado: ");
  Serial.print(path);
  Serial.print(" = ");
  Serial.println(value ? "true" : "false");

  return true;
}

void processQueue() {
  if (queueSize == 0 || firebaseURL.length() == 0) {
    return;
  }

  Serial.print("📨 Procesando cola: ");
  Serial.print(queueSize);
  Serial.println(" eventos");

  while (queueSize > 0) {
    Event currentEvent = eventQueue[queueStart];

    Serial.print("📤 Enviando cola: ");
    Serial.print(currentEvent.path);
    Serial.print(" = ");
    Serial.println(currentEvent.value ? "true" : "false");

    createFirebaseEvent("queued_event", currentEvent.path, currentEvent.value);

    queueStart = (queueStart + 1) % MAX_QUEUE_SIZE;
    queueSize--;
    delay(500);
  }

  Serial.println("✅ Cola procesada");
}

bool sendEvent(const String& path, bool value) {
  bool success = false;

  if (firebaseURL.length() > 0 && chipID_saved.length() > 0 && WiFi.status() == WL_CONNECTED) {
    String eventType = "sensor_" + path;
    String message = path + " = " + String(value);
    createFirebaseEvent(eventType, message, value);
    success = true;
  }

  if (!success) {
    success = addToQueue(path, value);
  }

  return success;
}

void checkFirebaseCommands() {
  // Para REST API, los comandos se pueden implementar leyendo un path específico
  // Por simplicidad, por ahora solo enviamos eventos
  // Se puede expandir después para leer comandos
}

// ==================== CONFIGURATION FUNCTIONS ====================

void initializeSystem() {
  // 1. Cargar configuración
  loadFactoryConfig();

  // 2. Si no hay configuración completa, ir a modo Factory
  if (chipID_saved.length() == 0 || firebaseHost.length() == 0) {
    Mode = "Factory";
    startAccessPoint();
    return;
  }

  // 3. Conectar WiFi
  connectToWiFi();

  // Si no se pudo conectar WiFi, continuar en modo local
  if (!wifiConnection) {
    Serial.println("⚠️  WiFi: Modo local (sin conexión)");
  }
}

void loadFactoryConfig() {
  preferences.begin("config_data", true);

  if (preferences.isKey("chipID")) {
    chipID_saved = preferences.getString("chipID", "");
  }

  firebaseHost = preferences.getString("firebaseHost", "");
  firebaseAuthToken = preferences.getString("firebaseAuth", "");
  firebaseBasePath = preferences.getString("firebasePath", "/dispositivos/");

  preferences.end();
}

void saveFactoryConfig() {
  preferences.begin("config_data", false);
  preferences.putString("chipID", chipID_saved);
  preferences.putString("firebaseHost", firebaseHost);
  preferences.putString("firebaseAuth", firebaseAuthToken);
  preferences.putString("firebasePath", firebaseBasePath);
  preferences.end();

  Serial.println("✅ Configuración guardada");
}

bool connectToWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnection = true;
    return true;
  }

  if (saved_ssid.length() == 0) {
    wifiConnection = false;
    Serial.println("❌ WiFi: No hay credenciales guardadas");
    return false;
  }

  WiFi.disconnect(true);
  delay(100);
  WiFi.mode(WIFI_STA);

  Serial.print("📶 Conectando a WiFi: ");
  Serial.println(saved_ssid);

  WiFi.begin(saved_ssid.c_str(), saved_password.c_str());

  unsigned long startAttemptTime = millis();
  const long wifiTimeout = 15000;

  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < wifiTimeout) {
    delay(500);
    Serial.print(".");
    processAlarmLogic();
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnection = true;
    Serial.println("");
    Serial.print("✅ WiFi: Conectado - IP: ");
    Serial.println(WiFi.localIP().toString());

    syncNTP();

    // Inicializar Firebase si tenemos configuración
    if (firebaseHost.length() > 0 && chipID_saved.length() > 0) {
      initFirebase();
    }

    return true;
  } else {
    wifiConnection = false;
    Serial.println("");
    Serial.println("❌ WiFi: Falló conexión");
    return false;
  }
}

void startAccessPoint() {
  const char* wm;
  if (Mode == "Factory") {
    wm = "FACTORY";
  } else {
    wm = "MAINTENANCE";
  }
  String ap_ssid_str = "Cerco-" + String(wm);
  const char* ap_ssid = ap_ssid_str.c_str();
  const char* ap_password = "";
  IPAddress apIP(192, 168, 4, 1);

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(ap_ssid, ap_password);

  dnsServer.start(DNS_PORT, "*", apIP);

  server.on("/", handleRoot);
  server.on("/connect", handleConnect);
  server.on("/factory_reset", handleFactoryReset);
  server.on("/factory", handleFactory);
  server.on("/save", handleSave);

  server.begin();
  Serial.print("🌐 AP iniciado: ");
  Serial.println(WiFi.softAPIP().toString());
}

bool isModeButtonPressed() {
  int readings = 0;
  for (int i = 0; i < 5; i++) {
    if (digitalRead(modeButton) == LOW) {
      readings++;
    }
    delay(10);
  }
  return (readings >= 3);
}

// ==================== WEB SERVER HANDLERS ====================

void handleRoot() {
  if (Mode.equals("Maintenance")) {
    int n = scanNetworks();

    String html = "<!DOCTYPE html><html lang='es'><head>";
    html += "<meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<title>Portal de Mantenimiento</title>";
    html += "<style>";
    html += "* { margin: 0; padding: 0; box-sizing: border-box; }";
    html += "body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); min-height: 100vh; display: flex; justify-content: center; align-items: center; padding: 20px; }";
    html += ".container { background: rgba(255, 255, 255, 0.95); backdrop-filter: blur(10px); border-radius: 20px; box-shadow: 0 15px 35px rgba(0, 0, 0, 0.1); padding: 40px; max-width: 500px; width: 100%; border: 1px solid rgba(255, 255, 255, 0.2); }";
    html += "h2 { color: #2c3e50; text-align: center; margin-bottom: 10px; font-size: 28px; font-weight: 600; }";
    html += "h3 { color: #34495e; margin: 25px 0 15px 0; font-size: 20px; border-bottom: 2px solid #3498db; padding-bottom: 8px; }";
    html += "h4 { color: #34495e; margin: 20px 0 10px 0; }";
    html += "label { display: block; margin-bottom: 8px; color: #2c3e50; font-weight: 500; }";
    html += "select, input[type='text'], input[type='password'] { width: 100%; padding: 12px 16px; border: 2px solid #e0e0e0; border-radius: 12px; font-size: 16px; transition: all 0.3s ease; background: #f8f9fa; margin-bottom: 15px; }";
    html += "select:focus, input:focus { outline: none; border-color: #3498db; background: white; box-shadow: 0 0 0 3px rgba(52, 152, 219, 0.1); }";
    html += "button { background: linear-gradient(135deg, #3498db, #2980b9); color: white; border: none; padding: 14px 28px; border-radius: 12px; font-size: 16px; font-weight: 600; cursor: pointer; transition: all 0.3s ease; box-shadow: 0 4px 15px rgba(52, 152, 219, 0.3); width: 100%; margin-top: 10px; }";
    html += "button:hover { transform: translateY(-2px); box-shadow: 0 6px 20px rgba(52, 152, 219, 0.4); background: linear-gradient(135deg, #2980b9, #3498db); }";
    html += "button:active { transform: translateY(0); }";
    html += ".section { background: #f8f9fa; padding: 20px; border-radius: 15px; margin: 20px 0; border-left: 4px solid #3498db; }";
    html += "hr { border: none; height: 1px; background: linear-gradient(to right, transparent, #bdc3c7, transparent); margin: 25px 0; }";
    html += ".status { background: #e8f4fd; color: #2980b9; padding: 12px; border-radius: 10px; text-align: center; margin: 15px 0; border: 1px solid #3498db; }";
    html += "</style>";
    html += "</head><body>";

    html += "<div class='container'>";
    html += "<h2>🔧 Portal de Mantenimiento</h2>";
    html += "<div class='status'>Modo: Mantenimiento - Configure la conexión WiFi</div>";

    html += "<div class='section'>";
    html += "<h3>📶 Configurar Conexión WiFi</h3>";
    html += "<form action='/connect' method='POST'>";
    html += "<label for='ssid'>Red WiFi Disponible:</label>";
    html += "<select name='ssid' id='ssid'>";

    if (n > 0) {
      for (int i = 0; i < n; ++i) {
        html += "<option value='" + WiFi.SSID(i) + "'>" + WiFi.SSID(i);
        html += " (" + String(WiFi.RSSI(i)) + " dBm)</option>";
      }
    } else {
      html += "<option value=''>No se encontraron redes</option>";
    }
    html += "</select>";

    html += "<label for='password'>Contraseña:</label>";
    html += "<input type='password' name='password' placeholder='Ingrese la contraseña' required>";
    html += "<button type='submit'>🔗 Conectar y Guardar</button>";
    html += "</form>";
    html += "</div>";

    html += "<hr>";

    html += "<div class='section'>";
    html += "<h4>⚙️ Restablecimiento de Fábrica</h4>";
    html += "<form action='/factory_reset' method='POST'>";
    html += "<label for='confirm'>Escribe <b>RESET</b> para confirmar:</label>";
    html += "<input type='text' name='confirm' placeholder='RESET' required>";
    html += "<button type='submit' style='background: linear-gradient(135deg, #e74c3c, #c0392b);'>🔄 Restablecer Fábrica</button>";
    html += "</form>";
    html += "</div>";

    html += "</div>";
    html += "</body></html>";

    server.send(200, "text/html", html);
  } else if (Mode.equals("Factory")) {
    server.sendHeader("Location", "/factory");
    server.send(302, "text/plain", "");
  }
}

void handleFactory() {
  String html = "<!DOCTYPE html><html lang='es'><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>Configuración de Fábrica</title>";
  html += "<style>";
  html += "* { margin: 0; padding: 0; box-sizing: border-box; }";
  html += "body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background: linear-gradient(135deg, #ff7e5f 0%, #feb47b 100%); min-height: 100vh; display: flex; justify-content: center; align-items: center; padding: 20px; }";
  html += ".container { background: rgba(255, 255, 255, 0.95); backdrop-filter: blur(10px); border-radius: 20px; box-shadow: 0 15px 35px rgba(0, 0, 0, 0.1); padding: 40px; max-width: 500px; width: 100%; border: 1px solid rgba(255, 255, 255, 0.2); }";
  html += "h3 { color: #2c3e50; text-align: center; margin-bottom: 25px; font-size: 24px; font-weight: 600; }";
  html += ".info-box { background: #fff3cd; border: 1px solid #ffeaa7; border-radius: 12px; padding: 15px; margin-bottom: 25px; text-align: center; color: #856404; }";
  html += ".help-text { background: #e8f4fd; border: 1px solid #3498db; border-radius: 8px; padding: 10px; margin: 10px 0; font-size: 14px; color: #2c3e50; }";
  html += "label { display: block; margin-bottom: 8px; color: #2c3e50; font-weight: 500; margin-top: 15px; }";
  html += "input { width: 100%; padding: 12px 16px; border: 2px solid #e0e0e0; border-radius: 12px; font-size: 16px; transition: all 0.3s ease; background: #f8f9fa; }";
  html += "input:focus { outline: none; border-color: #ff7e5f; background: white; box-shadow: 0 0 0 3px rgba(255, 126, 95, 0.1); }";
  html += "button { background: linear-gradient(135deg, #ff7e5f, #feb47b); color: white; border: none; padding: 14px 28px; border-radius: 12px; font-size: 16px; font-weight: 600; cursor: pointer; transition: all 0.3s ease; box-shadow: 0 4px 15px rgba(255, 126, 95, 0.3); width: 100%; margin-top: 20px; }";
  html += "button:hover { transform: translateY(-2px); box-shadow: 0 6px 20px rgba(255, 126, 95, 0.4); }";
  html += ".button-secondary { background: linear-gradient(135deg, #95a5a6, #7f8c8d); margin-top: 10px; }";
  html += ".button-secondary:hover { background: linear-gradient(135deg, #7f8c8d, #95a5a6); }";
  html += "</style>";
  html += "</head><body>";

  html += "<div class='container'>";
  html += "<h3>🏭 Configuración de Fábrica</h3>";
  html += "<div class='info-box'>Configuración inicial del dispositivo - Complete todos los campos</div>";

  html += "<form action='/save' method='POST'>";

  // Campo Chip ID
  html += "<label for='chip_id'>ID del Dispositivo (Chip ID):</label>";
  html += "<div class='help-text'>Ingrese un ID único para este dispositivo. Ejemplo: Cerco_01.</div>";
  html += "<input type='text' name='chip_id' id='chip_id' value='" + chipID_saved + "' placeholder='Ej: Cerco_01' required>";

  // Campo Firebase Host
  html += "<label for='firebase_host'>Firebase Host:</label>";
  html += "<div class='help-text'>Solo el host sin https://. Ej: tu-proyecto.firebaseio.com</div>";
  html += "<input type='text' name='firebase_host' value='" + firebaseHost + "' placeholder='tu-proyecto.firebaseio.com' required>";

  // Campo Firebase Auth Token (opcional para REST)
  html += "<label for='firebase_auth'>Firebase Database Secret (opcional):</label>";
  html += "<div class='help-text'>Token de autenticación (opcional para REST API)</div>";
  html += "<input type='password' name='firebase_auth' value='" + firebaseAuthToken + "' placeholder='Opcional para REST API'>";

  // Campo Firebase Path
  html += "<label for='firebase_path'>Ruta Base en Firebase:</label>";
  html += "<div class='help-text'>Ruta donde se guardarán los datos. Ej: /dispositivos/</div>";
  html += "<input type='text' name='firebase_path' value='" + firebaseBasePath + "' placeholder='/dispositivos/'>";

  html += "<button type='submit'>💾 Guardar Configuración Firebase</button>";
  html += "</form>";

  html += "</div>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

void handleConnect() {
  String input_ssid = server.arg("ssid");
  String input_password = server.arg("password");

  if (input_ssid.length() == 0) {
    String errorHtml = "<!DOCTYPE html><html><head><meta charset='UTF-8'><style>";
    errorHtml += "body { font-family: Arial; background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); display: flex; justify-content: center; align-items: center; height: 100vh; margin: 0; }";
    errorHtml += ".container { background: white; padding: 40px; border-radius: 20px; box-shadow: 0 15px 35px rgba(0,0,0,0.1); text-align: center; max-width: 400px; }";
    errorHtml += "button { background: #3498db; color: white; border: none; padding: 12px 24px; border-radius: 8px; cursor: pointer; margin-top: 20px; }";
    errorHtml += "</style></head><body>";
    errorHtml += "<div class='container'><h2>❌ Error</h2><p>El SSID no puede estar vacío</p>";
    errorHtml += "<button onclick='history.back()'>⬅️ Volver</button></div></body></html>";
    server.send(400, "text/html", errorHtml);
    return;
  }

  preferences.begin("wifi_config", false);
  preferences.putString("ssid", input_ssid);
  preferences.putString("password", input_password);
  preferences.end();

  saved_ssid = input_ssid;
  saved_password = input_password;

  Serial.println("✅ WiFi: Credenciales guardadas");

  // CORRECCIÓN: Cambiar todas las referencias de 'html' a 'successHtml'
  String successHtml = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  successHtml += "<meta http-equiv='refresh' content='3;url=/'></head><body>";
  successHtml += "<style>";
  successHtml += "body { font-family: 'Segoe UI', Tahoma; background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); display: flex; justify-content: center; align-items: center; height: 100vh; margin: 0; }";
  successHtml += ".container { background: white; padding: 40px; border-radius: 20px; box-shadow: 0 15px 35px rgba(0,0,0,0.1); text-align: center; max-width: 450px; }";
  successHtml += ".success-icon { font-size: 48px; margin-bottom: 20px; }";
  successHtml += "h1 { color: #27ae60; margin-bottom: 15px; }";
  successHtml += "</style></head><body>";
  successHtml += "<div class='container'>";
  successHtml += "<div class='success-icon'>✅</div>";
  successHtml += "<h1>¡Configuración Guardada!</h1>";
  successHtml += "<p><strong>Red:</strong> " + saved_ssid + "</p>";
  successHtml += "<p>El dispositivo intentará conectarse al WiFi y reiniciará en modo operación.</p>";
  successHtml += "<p>Redirigiendo en 3 segundos...</p>";
  successHtml += "</div></body></html>";

  server.send(200, "text/html", successHtml);

  delay(1000);
  Mode = "Operation";
  modeBlinkInterval = 3000;

  WiFi.softAPdisconnect(true);
  dnsServer.stop();
  server.stop();
  connectToWiFi();
}

void handleFactoryReset() {
  String confirm_arg = server.arg("confirm");

  if (confirm_arg.equals("RESET")) {
    preferences.begin("config_data", false);
    preferences.clear();
    preferences.end();

    Serial.println("🔄 Restablecimiento de fábrica completado");

    String html = "<!doctype html><html><head><meta charset='utf-8'><title>Restablecimiento</title>";
    html += "<meta http-equiv='refresh' content='5; url=/'></head><body>";
    html += "<h1>Restablecimiento Completado</h1>";
    html += "<p>Configuración borrada. Reiniciando en 5 segundos...</p>";
    html += "</body></html>";
    server.send(200, "text/html", html);

    delay(5000);
    ESP.restart();
  } else {
    server.send(400, "text/html",
                "<h1>Error de Confirmación</h1>"
                "<p>Debe escribir <b>RESET</b> exactamente para confirmar.</p>");
  }
}

void handleSave() {
  if (!server.hasArg("chip_id") || !server.hasArg("firebase_host")) {
    server.send(400, "text/html",
                "<h1>Error</h1><p>Chip ID y Firebase Host son obligatorios</p>");
    return;
  }

  chipID_saved = server.arg("chip_id");
  firebaseHost = server.arg("firebase_host");
  firebaseAuthToken = server.arg("firebase_auth");
  firebaseBasePath = server.arg("firebase_path");

  if (firebaseBasePath.length() == 0) {
    firebaseBasePath = "/dispositivos/";
  }

  saveFactoryConfig();

  Mode = "Maintenance";

  WiFi.softAPdisconnect(true);
  dnsServer.stop();
  server.stop();

  startAccessPoint();
  modeBlinkInterval = 1000;

  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "Configuración guardada - Redirigiendo");
}

// ==================== UTILITY FUNCTIONS ====================

int scanNetworks() {
  int n = WiFi.scanNetworks();
  return n;
}

void blinkModeLED() {
  unsigned long currentMillis = millis();

  if (currentMillis - modePreviousMillis >= modeBlinkInterval) {
    modePreviousMillis = currentMillis;
    modeLedState = !modeLedState;
    digitalWrite(LEDmode, modeLedState);
  }
}

bool isFirebaseConnected() {
  if (firebaseURL.length() == 0 || chipID_saved.length() == 0) {
    return false;
  }

  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  HTTPClient http;
  String testURL = firebaseURL + "/.json?limitToFirst=1";

  http.begin(testURL);
  http.setTimeout(5000);

  int httpCode = http.GET();
  http.end();

  return (httpCode == 200);
}

void printSystemStatus() {
  Serial.println("=== ESTADO DEL SISTEMA ===");
  Serial.println("Modo: " + Mode);
  Serial.println("WiFi: " + String(WiFi.status() == WL_CONNECTED ? "Conectado" : "Desconectado"));
  Serial.println("Firebase: " + String(isFirebaseConnected() ? "Conectado" : "Desconectado"));
  Serial.println("Eventos en cola: " + String(queueSize));
  Serial.println("Sistema Armado: " + String(systemArmed ? "SI" : "NO"));
  Serial.println("Alarma Activa: " + String(alarmActive ? "SI" : "NO"));
  Serial.println("========================");
}

void verifyFirebaseConfig() {
  Serial.println("=== VERIFICACIÓN CONFIGURACIÓN FIREBASE ===");
  Serial.println("Chip ID: " + chipID_saved);
  Serial.println("Firebase Host: " + firebaseHost);
  Serial.println("Firebase Path: " + firebaseBasePath);
  Serial.println("===========================================");
}

// ==================== SETUP & LOOP ====================

void setup() {
  Serial.begin(115200);
  Serial.printf("Sketch cargado: %s\n", strrchr(__FILE__, '\\') ? strrchr(__FILE__, '\\') + 1 : __FILE__);
  Serial.println("🔔 Inicializando alarma de cerco...");

  // Configurar pines
  pinMode(modeButton, INPUT_PULLUP);
  pinMode(LEDmode, OUTPUT);
  pinMode(LEDalarm, OUTPUT);
  pinMode(LEDarm, OUTPUT);

  delay(100);

  // Configurar botones
  bounceC2.attach(C2, INPUT_PULLUP);
  bounceC1.attach(C1, INPUT_PULLUP);
  bounceC3.attach(C3, INPUT_PULLUP);
  bounceC4.attach(C4, INPUT_PULLUP);
  bounceC5.attach(C5, INPUT_PULLUP);
  bounceC2.interval(50);
  bounceC1.interval(50);
  bounceC3.interval(50);
  bounceC4.interval(50);
  bounceC5.interval(50);

  bounceArm.attach(armButton, INPUT_PULLUP);
  bounceArm.interval(50);

  bounceAlarm.attach(alarmButton, INPUT_PULLUP);
  bounceAlarm.interval(50);

  // Cargar WiFi config
  preferences.begin("wifi_config", true);
  saved_ssid = preferences.getString("ssid", "");
  saved_password = preferences.getString("password", "");
  preferences.end();

  if (saved_ssid.length() > 0) {
    Serial.print("📡 WiFi guardado: ");
    Serial.println(saved_ssid);
  }

  // Verificar botón de modo
  if (isModeButtonPressed()) {
    Mode = "Maintenance";
    Serial.println("🛠️  Modo: Mantenimiento (botón presionado)");
    startAccessPoint();
  } else {
    initializeSystem();
  }

  // Configurar LED según modo
  if (Mode.equals("Operation")) {
    modeBlinkInterval = 3000;
    Serial.println("⚙️  Modo: Operación");
  } else if (Mode.equals("Maintenance")) {
    modeBlinkInterval = 1000;
    Serial.println("🔧 Modo: Mantenimiento");
  } else if (Mode.equals("Factory")) {
    modeBlinkInterval = 500;
    Serial.println("🏭 Modo: Fábrica");
  }

  digitalWrite(LEDmode, LOW);
  digitalWrite(LEDalarm, LOW);
  digitalWrite(LEDarm, HIGH);

  Serial.println("✅ Sistema inicializado");
}

void loop() {
  if (Mode.equals("Maintenance") || Mode.equals("Factory")) {
    server.handleClient();
    dnsServer.processNextRequest();
    blinkModeLED();
  }

  if (Mode.equals("Operation")) {
    processAlarmLogic();

    if (WiFi.status() == WL_CONNECTED) {
      if (!wifiConnection) {
        wifiConnection = true;
        Serial.println("✅ WiFi: Reconectado");
        modeBlinkInterval = 3000;
        processQueue();
        syncNTP();
      }

      static unsigned long lastQueueProcess = 0;
      if (millis() - lastQueueProcess > 30000) {
        processQueue();
        lastQueueProcess = millis();
      }

      // Actualizar estado del dispositivo cada 10 segundos
      static unsigned long lastStateUpdate = 0;
      if (millis() - lastStateUpdate > 10000) {
        updateDeviceState();
        lastStateUpdate = millis();
      }

    } else {
      if (wifiConnection) {
        wifiConnection = false;
        Serial.println("❌ WiFi: Desconectado");
        modeBlinkInterval = 1000;
      }

      unsigned long currentMillis = millis();
      if (currentMillis - lastReconnectAttempt >= RECONNECT_INTERVAL) {
        lastReconnectAttempt = currentMillis;
        Serial.println("🔄 WiFi: Intentando reconexión...");
        connectToWiFi();
      }
    }

    blinkModeLED();

    static unsigned long lastCommandCheck = 0;
    if (millis() - lastCommandCheck > 3000) {  // Chequear cada 3 segundos
      checkDeviceCommandsFirebase();
      lastCommandCheck = millis();
    }
  }
}

// ==================== ALARM LOGIC ====================

void processAlarmLogic() {
  bounceC1.update();
  bounceC2.update();
  bounceC3.update();
  bounceC4.update();
  bounceC5.update();
  bounceArm.update();
  bounceAlarm.update();

  // Control de armado/desarmado
  if (bounceArm.pressed()) {
    systemArmed = !systemArmed;

    if (systemArmed) {
      Serial.println("🛡️  Sistema ARMADO - Vigilancia activa");
      digitalWrite(LEDarm, HIGH);
      sendEvent("estado/armado", true);
    } else {
      Serial.println("🔓 Sistema DESARMADO");
      resetAlarmState();
      digitalWrite(LEDarm, LOW);
      sendEvent("estado/armado", false);
    }
  }

  if (systemArmed) {
    // Verificar cambios en los sensores (EXACTAMENTE como en tu código)
    if (bounceC1.read() == HIGH && anyIntrusion1 == false) {
      anyIntrusion1 = true;
      anyIntrusion = true;
      Serial.println("🚨 Intrusión detectada - Cerco 1");
      sendEvent("sensores/c1", true);
    } else if (bounceC1.read() == LOW && anyIntrusion1 == true) {
      anyIntrusion1 = false;
      anyIntrusion = false;
      Serial.println("✅ Cerco 1 normalizado");
      sendEvent("sensores/c1", false);
    }

    if (bounceC2.read() == HIGH && anyIntrusion2 == false) {
      anyIntrusion2 = true;
      anyIntrusion = true;
      Serial.println("🚨 Intrusión detectada - Cerco 2");
      sendEvent("sensores/c2", true);
    } else if (bounceC2.read() == LOW && anyIntrusion2 == true) {
      anyIntrusion2 = false;
      anyIntrusion = false;
      Serial.println("✅ Cerco 2 normalizado");
      sendEvent("sensores/c2", false);
    }

    if (bounceC3.read() == HIGH && anyIntrusion3 == false) {
      anyIntrusion3 = true;
      anyIntrusion = true;
      Serial.println("🚨 Intrusión detectada - Cerco 3");
      sendEvent("sensores/c3", true);
    } else if (bounceC3.read() == LOW && anyIntrusion3 == true) {
      anyIntrusion3 = false;
      anyIntrusion = false;
      Serial.println("✅ Cerco 3 normalizado");
      sendEvent("sensores/c3", false);
    }

    if (bounceC4.read() == HIGH && anyIntrusion4 == false) {
      anyIntrusion4 = true;
      anyIntrusion = true;
      Serial.println("🚨 Intrusión detectada - Cerco 4");
      sendEvent("sensores/c4", true);
    } else if (bounceC4.read() == LOW && anyIntrusion4 == true) {
      anyIntrusion4 = false;
      anyIntrusion = false;
      Serial.println("✅ Cerco 4 normalizado");
      sendEvent("sensores/c4", false);
    }

    if (bounceC5.read() == HIGH && anyIntrusion5 == false) {
      anyIntrusion5 = true;
      anyIntrusion = true;
      Serial.println("🚨 Intrusión detectada - Cerco 5");
      sendEvent("sensores/c5", true);
    } else if (bounceC5.read() == LOW && anyIntrusion5 == true) {
      anyIntrusion5 = false;
      anyIntrusion = false;
      Serial.println("✅ Cerco 5 normalizado");
      sendEvent("sensores/c5", false);
    }

    // Activar alarma si hay nueva intrusión
    if (anyIntrusion && !alarmActive) {
      alarmActive = true;
      alarmStartTime = millis();
      digitalWrite(LEDalarm, HIGH);
      Serial.println("🔥 ¡ALARMA ACTIVADA! Intrusión detectada");
      sendEvent("estado/alarma", true);
      
      alarmDataBase = true;
    }

    if (anyIntrusion6 && !anyIntrusion) {
      alarmActive = true;
      anyIntrusion = true;
      alarmStartTime = millis();
      digitalWrite(LEDalarm, HIGH);
      Serial.println("🔥 Alarma activada manualmente anteriormente");
      sendEvent("estado/alarma", true);
      
      alarmDataBase = true;
    }

    // Verificar timeout de alarma
    if (alarmActive) {
      checkAlarmTimeout();
    }

    // Control manual de alarma
    if (bounceAlarm.pressed()) {
      if (alarmActive) {
        Serial.println("👆 Alarma desactivada manualmente");
        anyIntrusion6 = false;
        anyIntrusion = false;
        resetAlarmState();
      } else {
        Serial.println("🔥 Alarma activada manualmente");
        alarmActive = true;
        anyIntrusion = true;
        anyIntrusion6 = true;
        alarmStartTime = millis();
        digitalWrite(LEDalarm, HIGH);
        sendEvent("estado/alarma", true);
        alarmDataBase = true;
      }
    }

    // Desactivar alarma si no hay intrusiones
    if (!anyIntrusion && alarmActive) {
      Serial.println("✅ Alarma: Todos los cercos seguros - Alarma desactivada");
      resetAlarmState();
    }
  }
}

// Función para verificar timeout de alarma
void checkAlarmTimeout() {
  if (millis() - alarmStartTime >= ALARM_DURATION) {
    Serial.println("⏰ Alarma desactivada por tiempo");
    resetAlarmState();
  }
}

// Función auxiliar para resetear estado de alarma
void resetAlarmState() {
  anyIntrusion = false;
  alarmActive = false;
  digitalWrite(LEDalarm, LOW);
  Serial.println("💤 Estado de alarma reseteado");
  sendEvent("estado/alarma", false);
}

void updateDeviceState() {
  if (firebaseURL.length() == 0 || chipID_saved.length() == 0) {
    return;
  }

  unsigned long currentTime = getUnixTimestamp();

  DynamicJsonDocument doc(512);

  // Estado actual
  JsonObject state = doc.createNestedObject("state");
  state["system_armed"] = systemArmed;
  state["alarm_active"] = alarmActive;
  state["any_intrusion"] = anyIntrusion;

  // Sensores actuales
  JsonObject sensors = doc.createNestedObject("sensors");
  sensors["c1"] = anyIntrusion1;
  sensors["c2"] = anyIntrusion2;
  sensors["c3"] = anyIntrusion3;
  sensors["c4"] = anyIntrusion4;
  sensors["c5"] = anyIntrusion5;

  // Información de conexión
  doc["last_seen"] = currentTime;
  doc["rssi"] = WiFi.RSSI();
  doc["ip_address"] = WiFi.localIP().toString();

  String jsonStr;
  serializeJson(doc, jsonStr);

  String path = "/devices/" + chipID_saved;
  if (sendToFirebaseREST(path, jsonStr)) {
    Serial.print("📡 Estado actualizado: ");
    Serial.print("Armed=");
    Serial.print(systemArmed ? "SI" : "NO");
    Serial.print(", Alarm=");
    Serial.print(alarmActive ? "SI" : "NO");
    Serial.print(", Intrusión=");
    Serial.println(anyIntrusion ? "SI" : "NO");
  }
}

/*--------------------------------LEER ORDENES DE FIREBASE------------------------------------*/

// ===== FUNCIONES PARA LEER COMANDOS DE devices_commands =====

// Función para consultar comandos pendientes en Firebase
void checkDeviceCommandsFirebase() {
  if (WiFi.status() != WL_CONNECTED || chipID_saved.length() == 0 || firebaseURL.length() == 0) {
    return;
  }

  HTTPClient http;
  
  // MODIFICADO: Ahora busca directamente en /devices_commands/[chipID_saved]
  String url = firebaseURL + "/devices_commands/" + chipID_saved + ".json";
  
  http.begin(url);
  http.setTimeout(5000);
  
  int httpCode = http.GET();

  if (httpCode == 200) {
    String payload = http.getString();
    
    if (payload.length() > 0 && payload != "null" && payload != "{}") {
      processCommandsFirebase(payload);
    } else {
      // Si no hay comandos, no imprimir nada (evita spam)
    }
  } else if (httpCode == 404) {
    // La ruta /devices_commands/[chipID] no existe aún, es normal
    // No imprimir para evitar spam
  }
  // Solo imprimir errores críticos (opcional)
  
  http.end();
}

// Función para marcar comando como ejecutado en Firebase
bool markCommandAsExecutedFirebase(String commandId) {
  if (commandId.length() == 0 || chipID_saved.length() == 0) return false;

  HTTPClient http;
  // MODIFICADO: Ahora la ruta incluye el chipID
  String url = firebaseURL + "/devices_commands/" + chipID_saved + "/" + commandId + ".json";

  // Solo marcar como ejecutado, NO eliminar
  String updateData = "{\"executed\": true, \"status\": \"executed\", \"executed_at\": " + String(getUnixTimestamp()) + "}";

  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(5000);

  int httpCode = http.PATCH(updateData);
  http.end();

  bool success = (httpCode == 200);

  if (success) {
    Serial.println("[FIREBASE] ✅ Comando marcado como ejecutado");
  } else {
    Serial.println("[FIREBASE] ❌ Error marcando comando como ejecutado");
  }

  return success;
}

// Función para procesar y ejecutar comandos de Firebase
void processCommandsFirebase(String payload) {
  // Parsear el JSON de Firebase
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    Serial.println("[FIREBASE] ❌ Error parseando JSON de comandos: " + String(error.c_str()));
    return;
  }

  // Iterar sobre todos los comandos (ahora están directamente bajo el chipID)
  for (JsonPair kv : doc.as<JsonObject>()) {
    String commandId = kv.key().c_str();
    JsonObject commandData = kv.value().as<JsonObject>();

    // Verificar si el comando ya fue ejecutado
    bool executed = commandData["executed"] | false;
    if (executed) {
      continue;  // Saltar comandos ya ejecutados
    }

    // Obtener datos del comando
    String command = commandData["command"] | "";
    String sensor = commandData["sensor"] | "";
    String deviceId = commandData["device_id"] | "";  // Por si acaso

    // Verificar que el comando sea para este dispositivo (doble verificación)
    if (deviceId.length() > 0 && deviceId != chipID_saved) {
      continue;  // Saltar comandos destinados a otros dispositivos
    }

    // Mostrar comando recibido
    Serial.print("[FIREBASE] Comando recibido: ");
    if (command == "ACTIVAR_ALARMA") Serial.print("Activando alarma");
    else if (command == "DESACTIVAR_ALARMA") Serial.print("Desactivando alarma");
    else if (command == "ARMAR_SISTEMA") Serial.print("Armando sistema");
    else if (command == "DESARMAR_SISTEMA") Serial.print("Desarmando sistema");
    else if (command == "ACTIVAR_SENSOR") Serial.print("Activando sensor " + sensor);
    else if (command == "DESACTIVAR_SENSOR") Serial.print("Desactivando sensor " + sensor);
    else Serial.print(command);
    Serial.println(" (remoto)");

    // Ejecutar la acción según el comando
    bool comandoReconocido = false;

    if (command == "ACTIVAR_ALARMA" || command == "ALARMAR") {
      anyIntrusion6 = true;
      anyIntrusion = true;
      alarmStartTime = millis();
      digitalWrite(LEDalarm, HIGH);
      sendEvent("estado/alarma", true);
      alarmActive = true;
      comandoReconocido = true;

    } else if (command == "DESACTIVAR_ALARMA" || command == "DESALARMAR") {
      anyIntrusion6 = false;
      anyIntrusion = false;
      resetAlarmState();
      comandoReconocido = true;

    } else if (command == "ARMAR_SISTEMA" || command == "ARMAR") {
      systemArmed = true;
      digitalWrite(LEDarm, HIGH);
      sendEvent("estado/armado", true);
      comandoReconocido = true;

    } else if (command == "DESARMAR_SISTEMA" || command == "DESARMAR") {
      systemArmed = false;
      digitalWrite(LEDarm, LOW);
      resetAlarmState();
      sendEvent("estado/armado", false);
      comandoReconocido = true;

    } else if (command == "ACTIVAR_SENSOR" && sensor.length() > 0) {
      if (sensor == "c1") anyIntrusion1 = true;
      else if (sensor == "c2") anyIntrusion2 = true;
      else if (sensor == "c3") anyIntrusion3 = true;
      else if (sensor == "c4") anyIntrusion4 = true;
      else if (sensor == "c5") anyIntrusion5 = true;
      
      sendEvent("sensores/" + sensor, true);
      comandoReconocido = true;

    } else if (command == "DESACTIVAR_SENSOR" && sensor.length() > 0) {
      if (sensor == "c1") anyIntrusion1 = false;
      else if (sensor == "c2") anyIntrusion2 = false;
      else if (sensor == "c3") anyIntrusion3 = false;
      else if (sensor == "c4") anyIntrusion4 = false;
      else if (sensor == "c5") anyIntrusion5 = false;
      
      sendEvent("sensores/" + sensor, false);
      comandoReconocido = true;

    } else {
      Serial.println("[FIREBASE] ⚠️ Comando no reconocido: " + command);
    }

    // Si el comando fue reconocido, marcarlo como ejecutado
    if (comandoReconocido) {
      markCommandAsExecutedFirebase(commandId);
      
      // También puedes enviar una confirmación
      String confirmMsg = "Comando " + command + " ejecutado exitosamente";
      createFirebaseEvent("command_executed", confirmMsg, true);
    }
  }
}