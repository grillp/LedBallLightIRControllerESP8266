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
const int pin_led = LED_BUILTIN;
const int pin_button = 0;
const unsigned int capture_buffer_size = 150;

IRsend irsend(pin_ir_send);

Ticker ticker;

typedef struct _command_data {
  String url;
  uint64_t code;
  bool prefix_with_on;
} command_data;

#define IRCODE_ON 0x1FE48B7UL
#define IRCODE_OFF 0x1FE7887UL
#define IRCODE_RED 0X1FE50AFUL
#define IRCODE_YELLOW 0x1FE30CFUL
#define IRCODE_WHITE 0x1FE906FUL
#define IRCODE_BLUE 0x1FEF807UL
#define IRCODE_LIGHTBLUE 0x1FE708FUL
#define IRCODE_GREEN 0x1FED827UL
#define IRCODE_PURPLE 0x1FEB04FUL
#define IRCODE_BRIGHTNESS 0x1FEE01FUL
#define IRCODE_CYCLE 0x1FE807FUL

command_data COMMAND_DATA[] =
{
  { "on", IRCODE_ON, false },
  { "off", IRCODE_OFF, false },
  { "red", IRCODE_RED, true },
  { "yellow", IRCODE_YELLOW, true },
  { "white", IRCODE_WHITE, true },
  { "blue", IRCODE_BLUE, true },
  { "lightblue", IRCODE_LIGHTBLUE, true },
  { "green", IRCODE_GREEN, true },
  { "purple", IRCODE_PURPLE, true },
  { "brightness", IRCODE_BRIGHTNESS, false },
  { "cycle", IRCODE_CYCLE, true },
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
  StaticJsonBuffer<100> jsonBuffer;
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

void loadFomColorString(String colorString)
{
  int idx = 0;
  int number = 0;
  for (uint i=1; i<colorString.length(); i++) // Ignore ( at the start
  {
    char c = colorString[i];
    if (c>='0' && c<= '9') {
        number *= 10;
        number += (int)(c-'0');
    }
    else {
      ball_rgb_color[idx++]=number;
      number=0;
    }
  }
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

void handleColorCommand() {
  String colorArg = server.arg("c");
  if (colorArg) {
    loadFomColorString(colorArg);
    sendStateResponse();
  } else {
    handleNotFound();
  }
}

void setupServer()
{
  for(uint i=0; i < sizeof(COMMAND_DATA)/sizeof(command_data); i++) {
    _command_data *cmd = &(COMMAND_DATA[i]);
    server.on(String("/")+cmd->url,[cmd]() {
      if (cmd->prefix_with_on) {
        sendIRCode(IRCODE_ON);
        delay(500);
      }
      sendIRCode(cmd->code);
      sendStateResponse();
    });
  }
  server.on("/", handleStatus);
  server.on("/state", handleStatus);
  server.on("/color", handleColorCommand);
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
