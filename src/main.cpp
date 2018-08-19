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
const int delay_between_ircodes = 250;
#define MAX_BRIGHTNESS_LEVEL 3

IRsend irsend(pin_ir_send);

Ticker ticker;

typedef struct _color_command_data {
  String url;
  uint64_t code;
} color_command_data;

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

void handleBrighntness();

color_command_data COLOR_COMMAND_DATA[] =
{
  { "red", IRCODE_RED },
  { "yellow", IRCODE_YELLOW },
  { "white", IRCODE_WHITE },
  { "blue", IRCODE_BLUE },
  { "lightblue", IRCODE_LIGHTBLUE },
  { "green", IRCODE_GREEN },
  { "purple", IRCODE_PURPLE },
  { "cycle", IRCODE_CYCLE },
};

// RGB color approximations for each LED color
// Used for nearestmRGB color approximations
typedef struct led_colors {
  uint64_t color_ir_code;
  uint r, g, b;
} _led_colors;

led_colors LED_COLOR_LOOKUP[] = {
    {IRCODE_WHITE, 245, 245, 245},
    {IRCODE_RED, 139, 0, 0},
    {IRCODE_GREEN, 0, 255, 0},
    {IRCODE_BLUE, 0, 0, 139},
    {IRCODE_YELLOW, 255, 165, 0},
    {IRCODE_PURPLE, 128, 0, 128},
    {IRCODE_LIGHTBLUE, 0, 255,255},
    {IRCODE_CYCLE, 0, 0,0},
};

// Current LED Ball State
bool        ball_on = false;
led_colors *ball_color = &LED_COLOR_LOOKUP[0];
int         ball_brightness = MAX_BRIGHTNESS_LEVEL;

ESP8266WebServer server(80);
MDNSResponder mdns;

const char KEY_STATE[]="state";
const char KEY_BRIGHTNESS[]="brightness";
const char KEY_COLOR[]="color";


// ------------------------------------
// Helper routines
// ------------------------------------
bool ifPressedAndIdle(int pin) {
  return !ticker.active() && (digitalRead(pin) == LOW);
}

void serialPrintUri() {
  Serial.print("cmd: ");
  Serial.print(server.uri());
  if (server.args()>0) Serial.write('?');
  for (int i=0; i<server.args(); i++) {
    if (i!=0) Serial.write('&');
    Serial.print(server.argName(i));
    Serial.print('=');
    Serial.print(server.arg(i));
  }
  Serial.println();
}

#if USE_LED_BUILTIN
// Device LED Routines
void ledBuiltinOff()
{
  Serial.println("LED off");
  digitalWrite(pin_led, HIGH);                          // Shut down the LED
  pinMode(pin_led, INPUT);
  ticker.detach();                                      // Stopping the ticker
}

void ledBuiltinOn(float ms=1000)
{
  Serial.print("LED on for ");
  Serial.print(ms, DEC);
  Serial.println("ms");
  pinMode(pin_led, OUTPUT);
  digitalWrite(pin_led, LOW);                           // Turn on the LED for 0.5 seconds
  ticker.attach_ms(ms, ledBuiltinOff);
}

#endif

// IR Sending Code
void sendIRCode(uint64_t code)
{
    irsend.sendNEC(code);
    #if USE_LED_BUILTIN
      ledBuiltinOn();
    #endif
}

// State Management
void setStateColor(led_colors *color) {
  if (ball_on) {
    ball_color=color;
    ball_brightness=MAX_BRIGHTNESS_LEVEL;
    sendIRCode(color->color_ir_code);
  }
}

// Server code
String stateAsJson()
{
  String stateJson;
  StaticJsonBuffer<100> jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();
  JsonArray& array = root.createNestedArray(KEY_COLOR);
  array.add(ball_color->r);
  array.add(ball_color->g);
  array.add(ball_color->b);
  root[KEY_STATE] = ball_on ? "ON": "OFF";
  root[KEY_BRIGHTNESS] = ball_brightness;
  root.printTo(stateJson);
  return stateJson;
}

void sendStateResponse()
{
  String stateJson = stateAsJson();
  Serial.print("State: ");
  Serial.println(stateJson);
  server.send(200, "application/json", stateJson);
}

// -----------------------------------
// Color Calclations and lookup
// -----------------------------------
uint *extractRGBFromColorString(uint rgb[], String colorString)
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
      rgb[idx++]=number;
      number=0;
    }
  }
  return rgb;
}

#define CALC_DISTANCE(c1r, c1g, c1b, c2r, c2g, c2b) (sq(((double)c2r-(double)c1r)*0.30) + sq(((double)c2g-(double)c1g)*0.59) + sq(((double)c2b-(double)c1b)*0.11))

led_colors *closestColorIrCodeForStateRGB(uint rgb[]) {

  double match_distance = CALC_DISTANCE(0,0,0,255,255,255);
  uint match_index=0;
  for (uint i=0; i < sizeof(LED_COLOR_LOOKUP)/sizeof(led_colors); i++)
  {
    double distance = CALC_DISTANCE(rgb[0], rgb[1], rgb[2], LED_COLOR_LOOKUP[i].r, LED_COLOR_LOOKUP[i].g, LED_COLOR_LOOKUP[i].b);
    if (distance == match_distance) {
      return &LED_COLOR_LOOKUP[i];
    }
    if (distance < match_distance) {
      match_distance = distance;
      match_index = i;
    }
  }
  return &LED_COLOR_LOOKUP[match_index];
}

led_colors *findColorForIrCode(uint64_t ir_code)
{
  for (uint i=0; i < sizeof(LED_COLOR_LOOKUP)/sizeof(led_colors); i++)
  {
    if(LED_COLOR_LOOKUP[i].color_ir_code == ir_code) return &LED_COLOR_LOOKUP[i];
  }
  // return the current color if you can;t fnd one..
  return ball_color;
}

  // -----------------------------------
// Server handlers
// -----------------------------------
void handleStatus() {
  serialPrintUri();
  sendStateResponse();
}

void handleNotFound() {
  serialPrintUri();
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
  serialPrintUri();
  String colorArg = server.arg("c");
  if (colorArg) {
    uint color[3];
    setStateColor(closestColorIrCodeForStateRGB(extractRGBFromColorString(color, colorArg)));
    sendStateResponse();
  } else {
    handleNotFound();
  }
}

void handleBrightness() {
  serialPrintUri();
  if (ball_on) {
    bool first = true;
    int requiredLevel = server.arg("l").toInt();
    while (ball_brightness != requiredLevel) {
      if(!first) delay(delay_between_ircodes);
      sendIRCode(IRCODE_BRIGHTNESS);
      first=false;
      ball_brightness=(ball_brightness%MAX_BRIGHTNESS_LEVEL)+1;
    }
  }
  sendStateResponse();
}

void handleOn() {
  serialPrintUri();
  if (!ball_on) {
    ball_on=true;
    ball_brightness=MAX_BRIGHTNESS_LEVEL;
    sendIRCode(IRCODE_ON);
  }
  sendStateResponse();
}

void handleOff() {
  serialPrintUri();
  if (ball_on) {
    ball_on=false;
    sendIRCode(IRCODE_OFF);
  }
  sendStateResponse();
}


// -----------------------------------
// HTTP Server setup
// -----------------------------------
void setupServer()
{
  for(uint i=0; i < sizeof(COLOR_COMMAND_DATA)/sizeof(color_command_data); i++) {
    _color_command_data *cmd = &(COLOR_COMMAND_DATA[i]);
      server.on(String("/")+cmd->url,[cmd]() {
        serialPrintUri();
        setStateColor(findColorForIrCode(cmd->code));
        sendStateResponse();
      });
  }
  server.on("/", handleStatus);
  server.on("/on", handleOn);
  server.on("/off", handleOff);
  server.on("/state", handleStatus);
  server.on("/color", handleColorCommand);
  server.on("/brightness", handleBrightness);
  server.onNotFound(handleNotFound);
  server.begin();
}

// -----------------------------------
// Device Seetup
// -----------------------------------
void setup() {
  irsend.begin();
  sendIRCode(IRCODE_OFF);

  Serial.begin(115200);
  delay(1000);
  WiFi.begin(SSID_NAME, SSID_PW);
  Serial.println("");

  Serial.print("Waiting for WiFi.");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(WiFi.SSID());
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP().toString());

  if (mdns.begin(HOST_NAME, WiFi.localIP())) {
    Serial.print("Hostname address: ");
    Serial.print(HOST_NAME);
    Serial.print(".local");
  }

  // Turn Off LED
  pinMode(pin_led, OUTPUT);
  digitalWrite(pin_led, LOW);

  // Put Button into INPUT Mode
  pinMode(pin_button, INPUT);

  setupServer();

  Serial.println("");
  Serial.println("LED Ball Server Ready!");
  Serial.println("");
  Serial.print("Waiting for commands at: ");
  Serial.print("http://");
  Serial.print(WiFi.localIP().toString());
  Serial.println("/");

}

// -----------------------------------
// Main loop
// -----------------------------------
void loop() {

  server.handleClient();

  if (ifPressedAndIdle(pin_button)) {
    sendIRCode(IRCODE_OFF);
  }

  delay(200);

}
