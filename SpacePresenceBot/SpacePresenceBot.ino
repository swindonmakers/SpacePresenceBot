/* 
  Connections:

  NodeMCU <-> Switch Array
  3.3V
  GND
  A0 - Button(s)

  NodeMCU <-> LCD
  VIN (5V)
  GND
  D2 - SDA (purple)
  D1 - SCL (blue)

  NodeMCU <-> Card Reader
  3.3V
  GND
  D0 - RST (yellow)
  D4 - SDA (orange)
  D5 - SCK (blue)
  D6 - MISO (gray)
  D7 - MOSI (purple)

  D3 - NC
  D8 - NC

*/

#include "config.h"

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <Ticker.h>
#include <UniversalTelegramBot.h>
#include "MFRC522.h"
#include <PingKeepAlive.h>
#include <RhaNtp.h>
#include <Wire.h>
#include <LCD.h>
#include <LiquidCrystal_I2C.h>
#include <AnalogMultiButton.h>
#include "FS.h"

#define RST_PIN  16 // RST-PIN for RC522 - RFID - SPI - Modul GPIO5 
#define SS_PIN  2 // SDA-PIN for RC522 - RFID - SPI - Modul GPIO4 
#define PIN_LED LED_BUILTIN

Ticker ledticker; // Status led
LiquidCrystal_I2C  lcd(0x27,2,1,0,4,5,6,7); // 0x27 is the I2C bus address for an unmodified backpack
PingKeepAlive pka;
RhaNtp ntp;
WiFiClientSecure client;
UniversalTelegramBot bot(BOTtoken, client);
MFRC522 mfrc522(SS_PIN, RST_PIN); 

time_t bootTime = 0; // boot time
unsigned long lastDebug; // last time debug messages were send to serial
#define DEBUG_INTERVAL 5000

unsigned long telegramLastCheck; // last time telegram messages poll completed

unsigned long cardreaderLastCheck; // last time we polled the card reader
#define CARDREADER_CHECK_INTERVAL_MS 100

unsigned long lastTokenTime; // millis when last token was detected
#define TOKEN_DEBOUNCE_TIME_MS 5000

unsigned long lcdOffTime = 0; // next time (in millis()) when we should turn off the lcd backlight
#define LCD_ON_TIME 10000

char tokenStr[14]; // token as hex string
String lastToken; // last token as string

const int BUTTONS_TOTAL = 5;

// find out what the value of analogRead is when you press each of your buttons and put them in this array
// you can find this out by putting Serial.println(analogRead(BUTTONS_PIN)); in your loop() and opening the serial monitor to see the values
// make sure they are in order of smallest to largest
const int BUTTONS_VALUES[BUTTONS_TOTAL] = {8, 303, 459, 554, 618};

AnalogMultiButton buttons(A0, BUTTONS_TOTAL, BUTTONS_VALUES);

int stayTime = 0;
bool checkout = false;
String customName;
bool clearCustomName = false;

String formatNumber(int x)
{
  if (x < 10)
    return String("0") + String(x);
  else
    return String(x);
}

String formatTime(time_t t)
{
  return String(year(t)) 
    + "-" + String(formatNumber(month(t))) 
    + "-" + String(formatNumber(day(t)))
    + " " + String(formatNumber(hour(t))) 
    + ":" + String(formatNumber(minute(t))) 
    + ":" + String(formatNumber(second(t)));
}

void processTelegramMessages(int numNewMessages) {
  Serial.print(F("Telegram message received:"));
  Serial.println(numNewMessages);

  for (int i=0; i<numNewMessages; i++) {
    String chat_id = String(bot.messages[i].chat_id);
    String text = bot.messages[i].text;
    text.toLowerCase();

    String from_name = bot.messages[i].from_name;
    if (from_name == "") from_name = "Guest";
    String from_id = bot.messages[i].from_id;

    Serial.print("chat_id:"); Serial.println(chat_id);
    Serial.print("from_name:"); Serial.println(from_name);
    Serial.print("from_id:"); Serial.println(from_id);
    Serial.print("text:"); Serial.println(text);

    if (text.startsWith("/start") || text.startsWith("/about")) {
      Serial.println(F("Build welcome message"));
      String welcome = "Hello " + from_name + ", I'm the Space Presence Bot and I'll announce your name on Telegram when you ";
      welcome += "scan your access token on the Makerspace Check-in hardware by the door.\n";
      welcome += "If you'd like me to use a moniker other than the name you used when you signed up to the Makerspace you ";
      welcome += "can use the /callme command to set a custom name.  You can always clear whatever you set by using the ";
      welcome += "/resetname command.  Note that for both of these commands you also need to scan your token so make sure ";
      welcome += "you are in the space!";
      bot.sendMessage(chat_id, welcome, "Markdown");

    } else if (text.startsWith("/callme")) {
      String originalMessage = bot.messages[i].text;
      int i = originalMessage.indexOf(' ');
      if (i > 0) {
        customName = originalMessage.substring(i+1);
        bot.sendMessage(chat_id, "Okay, " + customName + ", please scan your card on the reader now. (Or let the display timeout to cancel)");
        lcdTwoLine("Scan your token", customName);
      } else {
        bot.sendMessage(chat_id, "If you'd like me to call you something else, please tell me what (eg /callme A.Maker) and then scan your card on the reader when prompted.");
      }

    } else if (text.startsWith("/resetname")) {
      clearCustomName = true;
      bot.sendMessage(chat_id, "Okay, please scan your card on the reader now to clear your custom moniker. (Or let the display timeout to cancel)");
      lcdTwoLine("Scan your token", "to reset name.");

    } else if (text.startsWith("/shownames") && from_id == ADMIN_ID) {
      String message;
      Dir dir = SPIFFS.openDir("/");
      while (dir.next()) {
        File f = dir.openFile("r");
        if (f) {
          String moniker = f.readStringUntil('\n');
          String realName = f.readStringUntil('\n');
          message += realName + " (" + dir.fileName().substring(1) + ") : " + moniker + "\n";
          f.close();
        }
      }
      bot.sendMessage(chat_id, message, "Markdown");

    } else if (text.startsWith("/remove") && from_id == ADMIN_ID) {
      int i = text.indexOf(' ');
      if (i > 0) {
        String tokenToRemove = text.substring(i+1);
        if (SPIFFS.exists("/" + tokenToRemove)) {
          SPIFFS.remove("/" + tokenToRemove);
          bot.sendMessage(chat_id, "Removed");
        } else {
          bot.sendMessage(chat_id, "Not found");
        }
      }

    } else if (text.startsWith("/debugdata") && from_id == ADMIN_ID) {
      Serial.println(F("Build debug message"));
      String message = "Millis: " + String(millis());
      message += "\nTime: " + formatTime(ntp.localNow());
      message += "\nBootTime: " + formatTime(bootTime);
      message += "\nFreeHeap: " + String(ESP.getFreeHeap());
      message += "\nLast Telegram: " + String(telegramLastCheck);
      message += "\nLast Reader: " + String(cardreaderLastCheck);
      message += "\nLast Token:" + String(lastTokenTime);
      
      bot.sendMessage(chat_id, message, "Markdown");

    } else if (text.startsWith("/initreader")) {
      Serial.println(F("Init card reader"));
      mfrc522.PCD_Init();
      yield();
      bot.sendMessage(chat_id, "Reader init done", "");

    } else {
      // unknown message
    }
  }
}

void tick()
{
  //toggle state
  int state = digitalRead(PIN_LED);  // get the current state of GPIO1 pin
  digitalWrite(PIN_LED, !state);     // set pin to the opposite state
}

//gets called when WiFiManager enters configuration mode
void configModeCallback (WiFiManager *myWiFiManager) {
  //entered config mode, make led toggle faster
  ledticker.attach(0.2, tick);
}

void wifiDisconnect()
{
  ledticker.attach(0.6, tick);
}

void wifiReconnect()
{
  ledticker.detach();
  digitalWrite(PIN_LED, HIGH);
}

void connectWifi()
{
  // start ticker with 0.5 because we start in AP mode and try to connect
  ledticker.attach(0.6, tick);
  
  WiFiManager wifiManager;
  //reset settings - for testing
  //wifiManager.resetSettings();
  wifiManager.setDebugOutput(true);

  //set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
  wifiManager.setAPCallback(configModeCallback);

  // Wait for at most 180 seconds for configuration to succeed
  wifiManager.setConfigPortalTimeout(180);
  
  if (!wifiManager.autoConnect(AP_NAME, AP_PWD)) {
    //reset and try again
    Serial.println(F("Wifi init failed, resetting..."));
    ESP.reset();
    delay(1000);
  }

  // if you get here you have connected to the WiFi
  Serial.println(F("Connected to wifi"));
  ledticker.detach();
  digitalWrite(PIN_LED, HIGH);
}

time_t requestTime()
{
  ntp.updateTime();
  return 0;
}

void updateTokenStr(const uint8_t *data, const uint32_t numBytes) {
  const char * hex = "0123456789abcdef";
  uint8_t b = 0;
  for (uint8_t i = 0; i < numBytes; i++) {
        tokenStr[b] = hex[(data[i]>>4)&0xF];
        b++;
        tokenStr[b] = hex[(data[i])&0xF];
        b++;
  }

  // null remaining bytes in string
  for (uint8_t i=numBytes; i < 7; i++) {
        tokenStr[b] = 0;
        b++;
        tokenStr[b] = 0;
        b++;
  }
}

// Helper routine to dump a byte array as hex values to Serial
void dump_byte_array(byte *buffer, byte bufferSize) {
  for (byte i = 0; i < bufferSize; i++) {
    Serial.print(buffer[i] < 0x10 ? " 0" : " ");
    Serial.print(buffer[i], HEX);
  }
}

void lcdTwoLine(String l1, String l2)
{
  lcd.clear();
  lcd.print(l1);
  lcd.setCursor(0,1);
  lcd.print(l2);
  lcd.setBacklight(HIGH);
  lcdOffTime = millis() + LCD_ON_TIME;
}

void processToken(String token)
{
  lcdTwoLine("Let me check", "your token...");
  unsigned long tStart = millis();

  HTTPClient http;
  http.begin(ACCESS_SYSTEM_HOST, ACCESS_SYSTEM_PORT, ACCESS_SYSTEM_URL + token);
  int httpCode = http.GET();
  if (httpCode > 0) {

    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();

      // Give human time to read lcd message when api call is quick
      while(tStart + 1500 > millis())
        yield();

      if (payload == "<No such person>") {
        Serial.println(token + " returned <no such person>");
        lcdTwoLine("Sorry, I don't", "know you.");

      } else {
        Serial.println(token + " identified as " + payload);

        if (customName != "") {
          // Save custom name
          if (SPIFFS.exists("/" + token))
            SPIFFS.remove("/" + token);
          File f = SPIFFS.open("/" + token, "w");
          if (f) {
            f.print(customName);
            f.print("\n");
            f.print(payload);
            f.print("\n");
            f.close();
            lcdTwoLine("Name saved", "");
          } else {
            lcdTwoLine("Error saving name", "Sorry.");
          }

          customName = "";
        
        } else if (clearCustomName) {
          // Remove custom name
          if (SPIFFS.exists("/" + token)) {
            SPIFFS.remove("/" + token);
            lcdTwoLine("Name reset to", "default.");
          }

          clearCustomName = false;

        } else {
          // Process checkin/out

          // Try get custom name
          String name = "";
          if (SPIFFS.exists("/" + token)) {
            Serial.println("Found custom name");
            File f = SPIFFS.open("/" + token, "r");
            if (f) {
              name = f.readStringUntil('\n');
              f.close();
            }
          }
          if (name == "" || name == NULL)
            name = payload;

          if (checkout) {
            lcdTwoLine("Goodbye", name);
            bot.sendMessage(GROUP_CHAT_ID, name + " has left the space.", "");

          } else {
            lcdTwoLine("Welcome!", name);
            if (stayTime > 0)
              bot.sendMessage(GROUP_CHAT_ID, name + " has arrived in the space and will be around for about " + String(stayTime) + " hours", "");
            else 
              bot.sendMessage(GROUP_CHAT_ID, name + " has arrived in the space.", "");
          }
        }
      }
    } else {
      Serial.print(token + " access server query failed, ");
      Serial.print(httpCode);
      Serial.print(" = ");
      Serial.println(http.errorToString(httpCode));
      lcdTwoLine("Sorry I couldn't", "check your id.");
    }
  } else {
    Serial.println(token + " access server query returned httpcode = 0");
    lcdTwoLine("Something went", "very wrong :(");
  }

  http.end();
}

void setStayTime(int hours)
{
  lcdTwoLine("Present id card", String(hours) + " hour stay");
  stayTime = hours;
  lastToken = "";
  checkout = false;
}

void setCheckout()
{
  lcdTwoLine("Present id card", "to checkout");
  stayTime = 0;
  lastToken = "";
  checkout = true;
}

void setup() 
{
  Serial.begin(115200);
  Serial.println(F("Space Presence Bot Startup"));
  
  pinMode(PIN_LED, OUTPUT);

  // Activate LCD module
  Wire.pins(D2, D1); //SDA, SCL
  lcd.begin (16,2); // for 16 x 2 LCD module
  lcd.setBacklightPin(3,POSITIVE);
  lcd.setBacklight(HIGH);
  lcdTwoLine("Space Check-in", "Connect wifi...");

  connectWifi();

  pka.onDisconnect(wifiDisconnect);
  pka.onReconnect(wifiReconnect);

  if (!MDNS.begin(MDNS_NAME)) {
    Serial.println(F("Failed to setup mdns"));
  } else {
    //MDNS.addService("http", "tcp", 80);
  }

  IPAddress timeServerIP;
  WiFi.hostByName(NTP_SERVER, timeServerIP);
  ntp.init(timeServerIP, TIMEZONE);
  setSyncProvider(requestTime);
  setSyncInterval(60 * 60); // every hour

  SPI.begin();
  mfrc522.PCD_Init();    // Init MFRC522

  SPIFFS.begin();

  lcdTwoLine("Present id card", "to check-in.");
  lcdOffTime = millis() + 10000;
}

void loop() 
{
  // Debug
  if (millis() > lastDebug + DEBUG_INTERVAL) {
    //Serial.println(ESP.getFreeHeap());
    lastDebug = millis();
  }

  // Housekeeping
  pka.loop();
  ntp.loop();
  if (bootTime == 0 && timeStatus() == timeSet)
    bootTime = ntp.localNow();

  // Buttons
  buttons.update();
  if (buttons.onPress(0)) setStayTime(1);
  else if (buttons.onPress(1)) setStayTime(2);
  else if (buttons.onPress(2)) setStayTime(3);
  else if (buttons.onPress(3)) setStayTime(4);
  else if (buttons.onPress(4)) setCheckout();
  
  // Check Telegram
  if (millis() > telegramLastCheck + TELEGRAM_CHECK_INTERVAL_MS)  {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    yield();

    while(numNewMessages) {
      processTelegramMessages(numNewMessages);
      numNewMessages = bot.getUpdates(bot.last_message_received + 1);
      yield();
    }

    telegramLastCheck = millis();
  }

  // Check card reader
  if (millis() > cardreaderLastCheck + CARDREADER_CHECK_INTERVAL_MS) {

    // Init the reader on every call to make sure its working correctly.
    // Checking version first doesn't seem a reliable way to test if its working
    // and the call to check card doesn't fail in any detecable way.
    mfrc522.PCD_Init();
    yield();

    if (mfrc522.PICC_IsNewCardPresent()) {
      Serial.print(F("Reader reports new card"));

      if (mfrc522.PICC_ReadCardSerial()) {
        lastTokenTime = millis();

        updateTokenStr(mfrc522.uid.uidByte, mfrc522.uid.size);

        Serial.print(F(" -> with UID: "));
        Serial.println(String(tokenStr));

        if (lastToken != String(tokenStr)) {
          lastToken = String(tokenStr);

          processToken(lastToken);
          stayTime = 0;
          checkout = false;
        }
      } else {
        Serial.println(F(" -> Failed to read card serial"));
      }
    } 
    yield();
    
    cardreaderLastCheck = millis();
  }

  // Token debounce
  if (lastToken != "" && millis() > lastTokenTime + TOKEN_DEBOUNCE_TIME_MS) {
    Serial.println(F("Clear last token"));
    lastToken = "";
    stayTime = 0;
    checkout = false;
    customName = "";
    clearCustomName = false;
  }

  // LCD Backlight timeout
  if (lcdOffTime !=0 && millis() > lcdOffTime) {
    Serial.println(F("Backlight off"));
    lcd.setBacklight(LOW);
    lcd.clear();
    lcdOffTime = 0;
    stayTime = 0;
    checkout = false;
    customName = "";
    clearCustomName = false;
  }
}
