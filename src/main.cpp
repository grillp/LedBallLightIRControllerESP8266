#include "IRremoteESP8266.h"
#include <IRsend.h>
#include <IRrecv.h>
#include <IRutils.h>

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>

#include <Ticker.h>

#include <ArduinoJson.h>

#include "env.h"

const int pin_ir_send = 12; // Writing Pin
const int pin_led = BUILTIN_LED;                               // Built in LED defined for WEMOS people
const int pin_button = 0;
const unsigned int capture_buffer_size = 150;                      // Size of the IR capture buffer.

IRsend irsend(pin_ir_send);

Ticker ticker;

typedef struct {
  String command;
  uint64_t code;
} color_command;

const color_command COLOR_COMMANDS[] =
{
  { "on", 0x1FE48B7UL },
  { "off", 0x1FE7887UL },
  { "red", 0X1FE50AFUL },
  { "yellow", 0x1FE30CFUL },
  { "white", 0x1FE906FUL },
  { "blue", 0x1FEF807UL },
  { "lightblue", 0x1FE708FUL },
  { "green", 0x1FED827UL },
  { "purple", 0x1FEB04FUL },
  { "brightness", 0x1FEE01FUL },
  { "cycle", 0x1FE807FUL },
};
//
// { "state",  }
// { "color",  }

bool   ball_on = false;
int    ball_rgb_color[] = {0,0,0};
int    ball_brightness = 3;

ESP8266WebServer server(80);
MDNSResponder mdns;

const char KEY_STATE[]="state";
const char KEY_BRIGHTNESS[]="brightness";
const char KEY_COLOR[]="color";

String stateAsJson()
{
  String stateJson;
  StaticJsonBuffer<500> jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();
  root[KEY_COLOR] = String("["+String(ball_rgb_color[0],DEC)+" ,"+String(ball_rgb_color[1],DEC)+" ,"+String(ball_rgb_color[2],DEC)+"]");
  root[KEY_STATE] = ball_on ? "ON": "OFF";
  root[KEY_BRIGHTNESS] = ball_brightness;
  root.printTo(stateJson);
  return stateJson;
}

void disableLed()
{
  Serial.println("LED off");
  digitalWrite(pin_led, HIGH);                          // Shut down the LED
  pinMode(pin_led, INPUT);
  ticker.detach();                                      // Stopping the ticker
}

void ledOn(float ms=1000)
{
  Serial.print("LED on for ");
  Serial.print(ms, DEC);
  Serial.println("ms");
  pinMode(pin_led, OUTPUT);
  digitalWrite(pin_led, LOW);                           // Turn on the LED for 0.5 seconds
  ticker.attach_ms(ms, disableLed);
}

void sendIRCode(uint64_t code)
{
    // Send IR Code
    irsend.sendNEC(code);
    ledOn();
}

void sendStateResponse()
{
    server.send(200, "application/json", stateAsJson());
}

void handleStatus() {
  sendStateResponse();
}

void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++)
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  server.send(404, "text/plain", message);
}

void setupServer()
{
  for(uint i=0; i < sizeof(COLOR_COMMANDS)/sizeof(color_command); i++) {
    server.on(String("/")+COLOR_COMMANDS[i].command,[i]() {
      sendIRCode(COLOR_COMMANDS[i].code);
      sendStateResponse();
    });
  }
  server.on("/", handleStatus);
  server.on("/status", handleStatus);
  server.onNotFound(handleNotFound);
  server.begin();
}


void setup() {
  irsend.begin();

  Serial.begin(115200);
  delay(1000);
  WiFi.begin(SSID, SSID_PW);
  Serial.println("");

  Serial.print("Waiting for WiFi.");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to LEXAM");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP().toString());

  if (mdns.begin(HOST_NAME, WiFi.localIP())) {
    Serial.println("MDNS responder started");
  }

  // Turn Off LED
  pinMode(pin_led, OUTPUT);
  digitalWrite(pin_led, LOW);

  // Put Button into INPUT Mode
  pinMode(pin_button, INPUT);

  setupServer();

  Serial.println("");
  Serial.println("ESP8266 IR Server");
  Serial.println("");
  Serial.println("Ready receive and send IR signals");

}

bool ifPressedAndIdle(int pin) {
  return !ticker.active() && (digitalRead(pin) == LOW);
}

void loop() {

  server.handleClient();

  if (ifPressedAndIdle(pin_button)) {
    sendIRCode(0x01FE48B7UL);
  }

  delay(200);

}
