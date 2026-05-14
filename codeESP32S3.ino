#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <BH1750.h>
#include <DHT11.h>

#define WIFI_SSID   "IoT project"
#define WIFI_PASS   "123456789"

#define MQTT_SERVER "172.168.1.2"
#define MQTT_PORT   1883
// Config static IP 
IPAddress local_IP(10, 10, 0, 50); // Địa chỉ IP của esp32
IPAddress gateway(10, 10, 0, 1);    // Địa chỉ IP của wifi
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(8, 8, 8, 8);     // DNS

#define WIFI_LED    6
#define DHTPIN      17

// Buttons according to schematic
#define BTN_UP_PIN    40   // UP
#define BTN_OK_PIN    41   // OK
#define BTN_DOWN_PIN  42   // DOWN

#define LAMP_PWM    10
#define PUMP_PIN    18

#define LCD_SDA     12
#define LCD_SCL     11

#define BH_SDA      16
#define BH_SCL      15

WiFiClient espClient;
PubSubClient client(espClient);

LiquidCrystal_I2C lcd(0x27, 16, 2);
TwoWire I2C_BH = TwoWire(1);
BH1750 lightMeter;
DHT11 dht11(DHTPIN);

// ===================== SYSTEM =====================
bool systemAutoMode = false;
bool lampState = false;
bool pumpState = false;
int pwmValue = 0;

float temp = 0;
float hum  = 0;
float lux  = 0;

// ===================== WIFI / MQTT STATE =====================
bool wifiOk = false;
bool mqttOk = false;

unsigned long lastWifiAttempt = 0;
unsigned long lastMqttAttempt = 0;
unsigned long lastPublishMs   = 0;

const unsigned long WIFI_RETRY_MS = 2000;   // 2 seconds
const unsigned long MQTT_RETRY_MS = 5000;
const unsigned long MQTT_PUB_MS   = 2000;

// ===================== MENU =====================
enum ScreenState
{
  SCREEN_MAIN,
  SCREEN_MENU,
  SCREEN_MODE_SELECT
};

ScreenState currentScreen = SCREEN_MAIN;

uint8_t menuIndex = 0;
uint8_t modeIndex = 0;

// ===================== BUTTON EDGE =====================
bool lastUpState = HIGH;
bool lastDownState = HIGH;
bool lastOkState = HIGH;

// =======================================================
// MQTT CALLBACK
// =======================================================
void callback(char* topic, byte* payload, unsigned int length)
{
  String msg = "";
  for (unsigned int i = 0; i < length; i++)
  {
    msg += (char)payload[i];
  }

  String t = String(topic);
  msg.trim();

  Serial.print("[MQTT] Topic: ");
  Serial.print(t);
  Serial.print(" | Msg: ");
  Serial.println(msg);

  if (t == "esp32s3/lamp")
  {
    if (msg == "ON")  lampState = true;
    if (msg == "OFF") lampState = false;
  }
  else if (t == "esp32s3/pump")
  {
    if (msg == "ON")  pumpState = true;
    if (msg == "OFF") pumpState = false;
  }
  else if (t == "esp32s3/autoLamp")
  {
    if (msg == "ON")  systemAutoMode = true;
    if (msg == "OFF") systemAutoMode = false;
  }
  else if (t == "esp32s3/autoPump")
  {
    // v2 chỉ có 1 chế độ auto chung, nên giữ tương thích topic cũ:
    // nhận ON => bật auto toàn hệ thống, OFF => tắt auto toàn hệ thống
    if (msg == "ON")  systemAutoMode = true;
    if (msg == "OFF") systemAutoMode = false;
  }
  else if (t == "esp32s3/mode")
  {
    // hỗ trợ thêm topic mode mới nếu app dùng topic này
    if (msg == "AUTO" || msg == "ON")  systemAutoMode = true;
    if (msg == "MANUAL" || msg == "OFF") systemAutoMode = false;
  }
}

// =======================================================
// BUTTON
// =======================================================
bool isButtonPressed(uint8_t pin, bool &lastState)
{
  bool currentState = digitalRead(pin);

  if (lastState == HIGH && currentState == LOW)
  {
    delay(10);
    if (digitalRead(pin) == LOW)
    {
      lastState = currentState;
      return true;
    }
  }

  lastState = currentState;
  return false;
}

// =======================================================
// SENSOR READ
// =======================================================
void readSensors()
{
  int temperature = 0;
  int humidity = 0;

  int result = dht11.readTemperatureHumidity(temperature, humidity);
  if (result == 0)
  {
    temp = temperature;
    hum  = humidity;
  }

  lux = lightMeter.readLightLevel();
  if (lux < 0) lux = 0;
}

// =======================================================
// LCD
// =======================================================
void drawMainScreen()
{
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("T:");
  lcd.print((int)temp);
  lcd.print(" H:");
  lcd.print((int)hum);

  lcd.setCursor(0, 1);
  if (systemAutoMode) lcd.print("AUTO ");
  else                lcd.print("MAN  ");

  if (wifiOk) lcd.print("WF:OK");
  else        lcd.print("WF:NO");
}

void drawMenuScreen()
{
  lcd.clear();

  lcd.setCursor(0, 0);
  if (menuIndex == 0) lcd.print(">");
  else                lcd.print(" ");
  lcd.print("Select mode");

  lcd.setCursor(0, 1);
  if (menuIndex == 1) lcd.print(">");
  else                lcd.print(" ");
  lcd.print("Exit");
}

void drawModeSelectScreen()
{
  lcd.clear();

  lcd.setCursor(0, 0);
  if (modeIndex == 0) lcd.print(">");
  else                lcd.print(" ");
  lcd.print("Auto");

  lcd.setCursor(0, 1);
  if (modeIndex == 1) lcd.print(">");
  else                lcd.print(" ");
  lcd.print("Manual");
}

// =======================================================
// OUTPUT CONTROL
// =======================================================
void handleOutputs()
{
  if (systemAutoMode)
  {
    pwmValue = map((int)lux, 0, 400, 255, 0);
    pwmValue = constrain(pwmValue, 0, 255);
    ledcWrite(0, pwmValue);

    if (temp > 30) pumpState = true;
    if(temp < 20) pumpState = false;
  }
  else
  {
    ledcWrite(0, lampState ? 255 : 0);
  }

  digitalWrite(PUMP_PIN, pumpState);
}

// =======================================================
// MENU CONTROL
// =======================================================
void handleMainButtons(bool up, bool down, bool ok)
{
  if (ok)
  {
    menuIndex = 0;
    currentScreen = SCREEN_MENU;
    drawMenuScreen();
    return;
  }

  if (!systemAutoMode)
  {
    if (up)   lampState = !lampState;
    if (down) pumpState = !pumpState;
  }
}

void handleMenuButtons(bool up, bool down, bool ok)
{
  if (up)
  {
    if (menuIndex > 0) menuIndex--;
    drawMenuScreen();
  }

  if (down)
  {
    if (menuIndex < 1) menuIndex++;
    drawMenuScreen();
  }

  if (ok)
  {
    if (menuIndex == 0)
    {
      modeIndex = systemAutoMode ? 0 : 1;
      currentScreen = SCREEN_MODE_SELECT;
      drawModeSelectScreen();
    }
    else
    {
      currentScreen = SCREEN_MAIN;
      drawMainScreen();
    }
  }
}

void handleModeButtons(bool up, bool down, bool ok)
{
  if (up)
  {
    if (modeIndex > 0) modeIndex--;
    drawModeSelectScreen();
  }

  if (down)
  {
    if (modeIndex < 1) modeIndex++;
    drawModeSelectScreen();
  }

  if (ok)
  {
    systemAutoMode = (modeIndex == 0);
    currentScreen = SCREEN_MENU;
    drawMenuScreen();
  }
}

// =======================================================
// WIFI HANDLER (every 2 seconds)
// =======================================================
void handleWiFi()
{
  if (WiFi.status() == WL_CONNECTED)
  {
    wifiOk = true;
    digitalWrite(WIFI_LED, LOW);
    return;
  }

  wifiOk = false;
  mqttOk = false;
  digitalWrite(WIFI_LED, HIGH);

  if (millis() - lastWifiAttempt >= WIFI_RETRY_MS)
  {
    lastWifiAttempt = millis();
    Serial.println("[WIFI] Reconnecting...");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASS);
  }
}

// =======================================================
// MQTT HANDLER
// =======================================================
void handleMQTT()
{
  if (!wifiOk) return;

  if (client.connected())
  {
    mqttOk = true;
    client.loop();
    return;
  }

  mqttOk = false;

  if (millis() - lastMqttAttempt >= MQTT_RETRY_MS)
  {
    lastMqttAttempt = millis();

    Serial.println("[MQTT] Connecting...");
    if (client.connect("ESP32S3_Client","admin","admin"))
    {
      mqttOk = true;
      Serial.println("[MQTT] Connected");

      // giữ tương thích topic của v1 + hỗ trợ topic mode mới
      client.subscribe("esp32s3/lamp");
      client.subscribe("esp32s3/pump");
      client.subscribe("esp32s3/autoLamp");
      client.subscribe("esp32s3/autoPump");
      client.subscribe("esp32s3/mode");
    }
    else
    {
      Serial.print("[MQTT] Failed, rc=");
      Serial.println(client.state());
    }
  }
}

// =======================================================
// MQTT PUBLISH
// =======================================================
void publishState()
{
  if (!mqttOk || !client.connected()) return;
  if (millis() - lastPublishMs < MQTT_PUB_MS) return;

  lastPublishMs = millis();

  char bufTemp[16];
  char bufHum[16];
  char bufLux[16];

  dtostrf(temp, 0, 1, bufTemp);
  dtostrf(hum,  0, 1, bufHum);
  dtostrf(lux,  0, 1, bufLux);

  client.publish("esp32s3/temp", bufTemp, true);
  client.publish("esp32s3/hum",  bufHum,  true);
  client.publish("esp32s3/lux",  bufLux,  true);

  client.publish("esp32s3/lampState", lampState ? "ON" : "OFF", true);
  client.publish("esp32s3/pumpState", pumpState ? "ON" : "OFF", true);
  client.publish("esp32s3/modeState", systemAutoMode ? "AUTO" : "MANUAL", true);
}

// =======================================================
// SETUP
// =======================================================
void setup()
{
  Serial.begin(115200);

  pinMode(WIFI_LED, OUTPUT);
  pinMode(BTN_UP_PIN, INPUT_PULLUP);
  pinMode(BTN_DOWN_PIN, INPUT_PULLUP);
  pinMode(BTN_OK_PIN, INPUT_PULLUP);
  pinMode(PUMP_PIN, OUTPUT);

  ledcSetup(0, 5000, 8);
  ledcAttachPin(LAMP_PWM, 0);
 // ledcAttach(LAMP_PWM,5000, 8);
  Wire.begin(LCD_SDA, LCD_SCL);
  lcd.begin(16,2);
  //lcd.init();
  lcd.backlight();

  I2C_BH.begin(BH_SDA, BH_SCL);
  lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, 0x23, &I2C_BH);

  client.setServer(MQTT_SERVER, MQTT_PORT);
  client.setCallback(callback);
  client.setBufferSize(256);
  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS)) {
    Serial.println("STA Failed to configure");
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  lcd.setCursor(0, 0);
  lcd.print("System Start");

  delay(800);
  readSensors();
  drawMainScreen();
}

// =======================================================
// LOOP
// =======================================================
void loop()
{
  bool up   = isButtonPressed(BTN_UP_PIN, lastUpState);
  bool ok   = isButtonPressed(BTN_OK_PIN, lastOkState);
  bool down = isButtonPressed(BTN_DOWN_PIN, lastDownState);

  // luôn duy trì WiFi/MQTT ở nền, nhưng WiFi chỉ retry mỗi 2s
  handleWiFi();
  handleMQTT();

  switch (currentScreen)
  {
    case SCREEN_MAIN:
      readSensors();
      handleMainButtons(up, down, ok);
      break;

    case SCREEN_MENU:
      handleMenuButtons(up, down, ok);
      break;

    case SCREEN_MODE_SELECT:
      handleModeButtons(up, down, ok);
      break;
  }

  handleOutputs();
  publishState();

  static unsigned long lastLCD = 0;
  if (currentScreen == SCREEN_MAIN && millis() - lastLCD > 1000)
  {
    drawMainScreen();
    lastLCD = millis();
  }

  delay(10);
}
