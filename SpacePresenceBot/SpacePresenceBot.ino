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

int Bot_mtbs = 1000; //mean time between scan messages
unsigned long Bot_lasttime; //last time messages' scan has been done

unsigned long card_lasttime; // last time a card was on the reader
int card_mbts = 50; // time between polling the card reader

unsigned long lcdOffTime = 0; // next time (in millis()) when we should turn off the lcd backlight

char tokenStr[14]; // token as hex string
String lastToken; // last token as string
unsigned long lastTokenTime; // millis when last token was presented
int tokenDebounceTime = 5000;

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

void handleNewMessages(int numNewMessages) {
  Serial.println("handleNewMessages");
  Serial.println(String(numNewMessages));

  for (int i=0; i<numNewMessages; i++) {
    String chat_id = String(bot.messages[i].chat_id);
    String text = bot.messages[i].text;

    String from_name = bot.messages[i].from_name;
    if (from_name == "") from_name = "Guest";

    if (text == "/chatid") {
      bot.sendMessage(chat_id, "The chat_id is: " + String(chat_id), "");
    } else if (text == "/start") {
      String welcome = "Welcome to the Space Presencee Bot, " + from_name + ".\n";

      welcome += "The time is " + formatTime(ntp.localNow());
      
      bot.sendMessage(chat_id, welcome, "Markdown");
    } else {
      //bot.sendMessage(chat_id, "I don't understand how to " + text, "");
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
  lcd.home();
  lcd.print("Space Check-in");
  lcd.setCursor(0,1);
  lcd.print("Connect wifi...");

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

  lcd.clear();
  lcd.print("Present id card");
  lcd.setCursor(0,1);
  lcd.print("to check-in.");
  lcdOffTime = millis() + 10000;
}

void loop() 
{
  pka.loop();
  ntp.loop();
  
  if (millis() > Bot_lasttime + Bot_mtbs)  {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);

    while(numNewMessages) {
      Serial.println(F("Message received"));
      handleNewMessages(numNewMessages);
      numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    }

    Bot_lasttime = millis();
  }

  if (millis() > card_lasttime + card_mbts) {
    if (mfrc522.PICC_IsNewCardPresent()) {
      if ( mfrc522.PICC_ReadCardSerial()) {
        lastTokenTime = millis();

        updateTokenStr(mfrc522.uid.uidByte, mfrc522.uid.size);

        if (lastToken != String(tokenStr)) {
                
          lastToken = String(tokenStr);

          Serial.print(F("Reader detected card with UID: "));
          Serial.print(String(tokenStr));
          Serial.println();

          lcd.clear();
          lcd.print("Let me check");
          lcd.setCursor(0,1);
          lcd.print("your token...");
          lcd.setBacklight(HIGH);
          lcdOffTime = millis() + 10000;
          unsigned long t1 = millis();

          HTTPClient http;
          http.begin(ACCESS_SYSTEM_API + lastToken);
          int httpCode = http.GET();
          if (httpCode > 0) {

            if (httpCode == HTTP_CODE_OK) {
              String payload = http.getString();

              // Give human time to read previous message if api call is quick
              while(t1+1500 > millis())
                yield();

              if (payload == "<No such person>") {
                Serial.println(lastToken + " returned <no such person>");
                lcd.clear();
                lcd.print("Sorry, I don't");
                lcd.setCursor(0,1);
                lcd.print("know you.");
              } else {
                Serial.println(lastToken + " identified as " + payload);
                lcd.clear();
                lcd.print("Welcome!");
                lcd.setCursor(0,1);
                lcd.print(payload);
                bot.sendMessage(GROUP_CHAT_ID, payload + " has arrived in the space.", "");
              }
            } else {
              Serial.print(lastToken + " access server query failed, ");
              Serial.print(httpCode);
              Serial.print(" = ");
              Serial.println(http.errorToString(httpCode));
              lcd.clear();
              lcd.print("Sorry I couldn't");
              lcd.setCursor(0,1);
              lcd.print("check your id.");
            }
          } else {
            Serial.println(lastToken + " access server query returned httpcode = 0");
            lcd.clear();
            lcd.print("Something went");
            lcd.setCursor(0,1);
            lcd.print("very wrong :(");
          }
          http.end();
          
          lcdOffTime = millis() + 10000;
        }
      }
    } else {
      if (millis() > lastTokenTime + tokenDebounceTime) {
        lastToken = "";
      }
    }

    card_lasttime = millis();
  }

  if (lcdOffTime !=0 && millis() > lcdOffTime) {
    lcd.setBacklight(LOW);
    lcd.clear();
    lcdOffTime = 0;
  }
}
