// ,------.             ,--.   ,--.                ,--. 
// |  .--. ' ,---.  ,---.\  `.'  /,---. ,--,--,  ,-|  | 
// |  '--' || .-. || .-. |\     /| .-. :|      \' .-. | 
// |  | --' ' '-' '| '-' ' \   / \   --.|  ||  |\ `-' | 
// `--'      `---' |  |-'   `-'   `----'`--''--' `---'  
//                 `--'                                 

// To configure your vending machine, edit ONLY the values HERE
// If you are in the Arduino IDE, make sure you have your partition scheme set to "Minimal SPIFFS" (Tools -> Partition Scheme)
// A list of libraries that must be installed is available HERE. Make sure these are installed before programming your board
// Ensure your popvend is plugged into your computer and you can see it connected above (Should say something like XIAO_ESP32C3). Check Tools -> Port. There should be some device shown there.
// Once you have filled in the values below and ensured device connection click the round "->" arrow button on the top left of the Arduino IDE

const char* DEVICE_TOKEN = "YOUR DEVICE TOKEN"; // ADD YOUR DEVICE TOKEN FROM YOUR TRIGR DEVICE DASHBOARD
const char* wifiSSID = "YOUR WIFI";  // ADD YOUR WIFI NAME
const char* wifipassword = "YOUR PASSWORD"; // ADD YOUR WIFI PASSWORD

String products[6] = {            // ADD THE PRODUCT IDS FROM THE PORTAL YOUR TRIGR DEVICE IS CONNECTED TO, IN ORDER
    "PRODUCT SHELF 1",            // They should look something like this: "8c53c8dd-7db9-4f37-8048-f13dbf81ddcd"
    "PRODUCT SHELF 2",
    "PRODUCT SHELF 3",
    "PRODUCT SHELF 4",
    "PRODUCT SHELF 5",
    "PRODUCT SHELF 6"
};

// Set the RGB values you want for your LEDs here
#define LEDR 255
#define LEDG 180
#define LEDB 45

// Set the idle scrolling text you want on the OLED
String defaultScrollText = "Hi! Scan me to get started.";

// DO NOT EDIT BELOW THIS POINT UNLESS YOU KNOW WHAT YOU ARE DOING!





















const char* websocket_url = "ws://192.168.1.136:3004";
const char* websocket_url_prod = "wss://trigr.dev";

#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <ESPAsyncWebServer.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoWebsockets.h>
#include <ESP32Servo.h>
#include <EEPROM.h>
#include <Wire.h>
#include <Adafruit_I2CDevice.h>
#include <Adafruit_NeoPixel.h>
#include <ArduinoJson.h>

#define FIRMWARE_VERSION "0.0.3"
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_ADDR 0x3C
#define EEPROM_SIZE 400
#define CONFIG_SETUP_ADDRESS 0  // Start address for WiFiConfig in EEPROM
#define CONFIG_SSID_ADDRESS 1  // Start address for WiFiConfig in EEPROM
#define CONFIG_PASSWORD_ADDRESS 60   // Start address for WiFiConfig in EEPROM
#define CAT9555_ADDR 0x27
#define PRODUCTION true
#define openingBlinkInterval 50
#define vendTime 1500
#define motorSpeed 70
#define motorSpeedReverse 115
#define NUM_SHELVES 6
#define VEND_TIMEOUT_MS 16000
#define SELECT_TIMEOUT_MS 1000 * 90
#define bounceExtendMs 600

#define SERVICE_UUID        "12345678-1234-1234-1234-1234567890ab"
#define CHARACTERISTIC_UUID "abcdefab-1234-1234-1234-abcdefabcdef"

const int retractionSignalPin = 8;

int lastShelfRetracted = -1;

unsigned long lastPingTime = 0;
const unsigned long pingInterval = 30000;  // Send a ping every 30 sec
unsigned long lastPongTime = 0;
const unsigned long pongTimeout = 50000;   // 90 sec without pong = force reconnect
unsigned long lastRetryTime = 0;
const unsigned long retryTimeout = 10000;   // retry every 10 seconds 

// Arrays to map shelf numbers to light and motor pins
#define LEDSignalPin 2 // GPIO 2
#define BBPin 3 // GPIO 3
const uint8_t motorPins[NUM_SHELVES] = {4,10,5,21,20,9};
Adafruit_NeoPixel pixels(NUM_SHELVES, LEDSignalPin, NEO_GRB + NEO_KHZ800);
bool shelfSelections[NUM_SHELVES] = {false, false, false, false, false, false};

const int NUM_SERVOS = 6;  // Number of servo motors
Servo servos[NUM_SERVOS];   // Array of Servo objects

// Variables to track the blink state for each shelf
unsigned long previousMillis = 0;
bool ledState = false;
const unsigned long selectionBlinkInterval = 500; // 1000 ms for on/off intervals

using namespace websockets;

int currentlyVending = -1;

String pin = "0000";
long pinExpiryTime = 0;

Adafruit_I2CDevice cat9555 = Adafruit_I2CDevice(CAT9555_ADDR, &Wire);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire);

int oledScrollX = 0;
String oledScrollText = "";

Servo m1;
Servo m2;
Servo m3;
Servo m4;
Servo m5;
Servo m6;

struct WiFiConfig {
  char ssid[32];
  char password[64];
  char uid[64];
  char id[64];
  bool setup;
};

WiFiConfig config;
bool restartRequested = false;

enum MachineState {
    STARTING,
    PROVISIONING,
    APACTIVE,
    RECONNECTING,
    CONNECTED,
    VENDING,
    LOADING,
    LOADBOUNCE,
    FAILURE,
    BEAM_OBSTRUCTED,
    WIFICONNECTFAILURE
};

const char* MachineStateNames[] = {
    "STARTING",
    "PROVISIONING",
    "APACTIVE",
    "RECONNECTING",
    "CONNECTED",
    "VENDING",
    "LOADING",
    "LOADBOUNCE",
    "FAILURE",
    "BEAM_OBSTRUCTED",
    "WIFICONNECTFAILURE"
};

// Pre-allocated buffer for messages to websocket
char messageBuffer[300]; // Adjust size based on expected output length

const char ssl_ca_cert[] PROGMEM = \
"-----BEGIN CERTIFICATE-----\n" \
"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\n" \
"-----END CERTIFICATE-----\n";


//const char* websocket_url = "wss://vendstaging.texelpad.com";


int blinkInterval = 2000;
int timeSinceBlinked = 0;
int vendStartTime = 0;
long lastSelectTime = 0;
bool configAPActive = false;
MachineState state = STARTING;  

WebsocketsClient client;
AsyncWebServer server(80);

// HTML form for configuring Wi-Fi credentials
const char* htmlForm = R"rawliteral(
  <!DOCTYPE HTML>
  <html>
  <head>
  </head>
  <body>
    <div class="content">
          <h1>Pop Vend</h1>
          <h3>Wifi Setup</h3>
          <form action="/get" method="get">
            <label>SSID</label>
            <input type="text" name="ssid"><br><br>
            <label>Password</label>
            <input type="password" name="password"><br><br>
            <input class="submit" type="submit" value="Submit">
          </form>
    </div>
  </body>
  <style>
    body { color:white ;font-family: "Verdana"; background-color: #00B5FF; font-size: 2em}
    h1 {font-family: "Brush Script MT"; font-size: 80px}
    .content {width: 95%; margin: auto; align-items: center; max-width: 600px}
    form {display:flex; flex-direction: column;}
    .submit {background-color: #30e000; color: white; font-size: 35px}
    input {height: 60px; border: none;}
    label {margin-bottom:5px; opacity: 0.7}
  </style>
  </html>
)rawliteral";

void setup() {
  Serial.begin(115200);

  config.setup = false;
  WiFi.onEvent(WiFiEvent);

  Wire.begin();

  // Setup OLED  
  display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  display.clearDisplay();
  display.setTextSize(4);
  display.setTextColor(SSD1306_WHITE);

  setScrollText("Starting up...");


  pixels.begin();

  //runMappingSequenceLights();
  //runMappingSequenceMotors();
  startupLightSequence(4);

  pinMode(BBPin, INPUT_PULLUP);
  

  for (uint8_t i = 0 ; i < NUM_SHELVES ; i++){
    pinMode(motorPins[i], OUTPUT);
    digitalWrite(motorPins[i], LOW);
  }

  pinMode(retractionSignalPin, INPUT_PULLUP);

  // Initialize EEPROM
  if (!EEPROM.begin(EEPROM_SIZE)) {
    //Serial.println("Failed to initialize EEPROM");
    state = FAILURE;
    return;
  }

  //clearWifiConfig();
  loadWifiConfig();

  Serial.println("WiFi configuration:");
  Serial.println(config.ssid);
  Serial.println(config.password);
  Serial.println(config.setup);

  // Start Websocket connection and normal funtionality
  if (config.setup) {
    //Serial.println("WiFi configured");
    connectToWifi(config.ssid, config.password);
  }
  // Start configuration AP
  else {
    Serial.println("WiFi NOT configured");
    startBluetooth();
    setAllLEDs(false);
    //setupAP();
    state = PROVISIONING;
  }

}


unsigned long lastWifiRetryTime = 0;      // Last time we attempted reconnect
const unsigned long retryWifiInterval = 120000; // Retry every 2 minutes if weak signal
const int maxWifiRetries = 0;             // 0 = retry forever, else cap retries
int retryCount = 0;

unsigned long lastLightMove = 0;      // Track light animation timing
int animateLightIndex = 0;                 // Which shelf/light is currently on

unsigned long loadStartTime = 0;       // Track when loading started
const unsigned long loadTimeout = 13000; // Timeout in milliseconds (example: 3 seconds)


int lastStateChecked = -1;
void checkForStateChange(){
  if (state != lastStateChecked){
    //Serial.print("Current state: ");
    //Serial.println(MachineStateNames[state]);
    lastStateChecked = state;
  }

}

void loop() {

  if (restartRequested){
    ESP.restart();
  }

  checkForStateChange();

  for (int i = 0; i < NUM_SHELVES; i++){
    if (shelfSelections[i] && millis() - lastSelectTime >= SELECT_TIMEOUT_MS){
      //sendCommand("TIMEOUT");
      client.send("{\"type\":\"TIMEOUT\",\"device\":\"" + String(DEVICE_TOKEN) + "\"}");
      clearSelection();
    }
  }

  if (oledScrollText.length() > 0){
    scrollTextTick();
  }
  
if (pinExpiryTime > 0 && millis() >= pinExpiryTime) {
    // Expire PIN here
    pin = "0000";
    setScrollText(defaultScrollText);
    pinExpiryTime = 0;  // clear it
}

  if (state == APACTIVE){
    //blinkPin(greenLEDPin);
  }

  else if (state == PROVISIONING){
    // Do something to indicate provisioning status
  }

  else if (state == FAILURE){
    //blinkPin(redLEDPin);
  }

  else if (state == WIFICONNECTFAILURE) {

      // Reconnect is handled by the WiFi library in the Event handler

      if (millis() - lastLightMove > 1000) { // Move every second
          setAllLEDs(false);
          setLED(animateLightIndex, true);

          animateLightIndex++;
          if (animateLightIndex >= NUM_SHELVES) animateLightIndex = 0;

          lastLightMove = millis();
      }

      // ---------------------
      // Check Wi-Fi status
      // ---------------------
      wl_status_t status = WiFi.status();

      // Determine action based on status / reason
      if (status == WL_NO_SSID_AVAIL || status == WL_CONNECT_FAILED) {
          // Invalid credentials - let softAP run in background, do nothing
          // Could optionally log or blink a different pattern
      } 
      else if (status == WL_DISCONNECTED || status == WL_CONNECTION_LOST) {
          // Likely weak signal / temporary drop
          if (millis() - lastWifiRetryTime > retryWifiInterval) {
              // Optional: cap retries if desired
              if (maxWifiRetries == 0 || retryCount < maxWifiRetries) {
                  //Serial.println("Retrying WiFi connection due to weak signal...");
                  WiFi.disconnect();
                  connectToWifi(config.ssid, config.password);
                  lastWifiRetryTime = millis();
                  retryCount++;
              }
          }
      }
      
  }

  else if (state == CONNECTED){
    blinkSelection();
    client.poll();
    //fallDetected();
      // Send a ping every 30 seconds to keep connection alive
      if (millis() - lastPingTime > pingInterval) {
          // Print WIFi signal strength
         // Serial.print("RSSI: ");
          //Serial.println(WiFi.RSSI());
          long rssiValue = WiFi.RSSI();             // get the RSSI
          String rssiStr = String(rssiValue);       // convert to String
          //sendParamCommand("SIG", rssiStr.c_str());  // pass as const char*
          client.ping();
          //Serial.println("Sent ping to server.");
          lastPingTime = millis();
      }

      // Check if we've missed a pong for too long (90 sec)
      if (millis() - lastPongTime > pongTimeout) {
          //Serial.println("No pong received in 90 seconds! Reconnecting WebSocket...");
          client.close();
          state = RECONNECTING;
          lastPongTime = millis();  // Reset timer to prevent immediate reconnect loops
      }

  }

  else if (state == RECONNECTING){
    if (millis() - lastRetryTime > retryTimeout) {
      //Serial.println("Retrying to connect to websocket...");
      connectToSocketServer();
      lastRetryTime = millis();  // Reset timer to prevent immediate reconnect loops
    }
  }

  else if (state == VENDING){
    //digitalWrite(redLEDPin, digitalRead(BBPin));

    if (fallDetected()){
      stopVend();
    }
    else if(millis() - vendStartTime >= VEND_TIMEOUT_MS){
      // Report vend failure
      client.send("{\"type\":\"ERROR\",\"device\":\"" + String(DEVICE_TOKEN) + "\"}");
      stopVend();
    }

  }

  else if (state == LOADING){
    
    if (loadStartTime == 0) {
        loadStartTime = millis();
    }
    
    if (retractionDetected() || (millis() - loadStartTime >= loadTimeout)){
    //Serial.println("Stop Load");
     stopLoad();
     startBounce();
     state = LOADBOUNCE;
     loadStartTime = 0;
    }
  }

  else if (state == LOADBOUNCE){
    if (!retractionDetected()){
      delay(bounceExtendMs);
      //Serial.println("Stop Bounce");
      stopBounce();
        state = CONNECTED;
    }
  }

  else if (state == BEAM_OBSTRUCTED){
    // Blink an LED
    if (!fallDetected()){
      delay(2000);
      state = VENDING;
      startVend();
    }
  }

}

bool retractionDetected(){
  return digitalRead(retractionSignalPin) == LOW;
}

void setLED(uint8_t index, bool on) {

  if (on){
    pixels.setPixelColor(NUM_SHELVES - 1 - index, pixels.Color(LEDR,LEDG,LEDB));
  }
  else{
    pixels.setPixelColor(NUM_SHELVES - 1 - index, pixels.Color(0,0,0));
  }
  pixels.show();

}

void setAllLEDs(bool on) {

  for (int i = 0; i < NUM_SHELVES ; i++ ){
    if (on){
      pixels.setPixelColor(i, pixels.Color(LEDR,LEDG,LEDB));
    }
    else{
      pixels.setPixelColor(i, pixels.Color(0,0,0));
    }
  }

  pixels.show();

}

// void setPin(uint8_t port, uint8_t pin, bool state) {
//   // Calculate the output register address (Port 0 = 0x02, Port 1 = 0x03)
//   uint8_t outputRegister = (port == 0) ? 0x02 : 0x03;

//   // Read the current state of the output register
//   uint8_t buffer[1];
//   cat9555.write_then_read(&outputRegister, 1, buffer, 1);

//   // Update the specific pin state
//   if (state) {
//     buffer[0] |= (1 << pin); // Set the bit for the pin
//   } else {
//     buffer[0] &= ~(1 << pin); // Clear the bit for the pin
//   }

//   // Write the updated value back to the output register
//   uint8_t writeData[2] = {outputRegister, buffer[0]};
//   cat9555.write(writeData, 2);
// }

// void setAllPins(bool state){
//   for (int i = 0; i < 8 ; i++ ){
//     setPin(0, i + 1, state);
//   }
// }

void startupLightSequence(int repetitions){

  setAllLEDs(false);

  for (int i = 0; i < repetitions; i++ ){

    setLED(0, true);
    setLED(2, true);
    setLED(4, true);
    delay(500);
    setAllLEDs(false);
    setLED(1, true);
    setLED(3, true);
    setLED(5, true);
    delay(500);
    setAllLEDs(false);
  }
}

void runMappingSequenceLights(){
  setAllLEDs(false);
  for (int i = 0; i < NUM_SHELVES; i++){
    setLED(i, true);
    delay(3000);
    setLED(i, false);
    delay(3000);
  }
  delay(2000);

  // for (int i = 0; i < NUM_SHELVES){
  //       m1.attach(motorPins[currentlyVending]);
  //       m1.write(motorSpeed);   
  // }

}

void runMappingSequenceMotors(){
  // setAllPins(LOW);
  // for (int i = 0; i < NUM_SHELVES; i++){
  //   shelfSelections[i] = true;
  //   //startVend();
  // }
  // delay(2000);

}

void startBounce(){
        //Serial.println("Start Bounce");
        if (lastShelfRetracted < 0){
          state = CONNECTED;
          return;
        }

        //Serial.println("Bounce shelf ");
        
        currentlyVending = lastShelfRetracted;

       // Serial.println(currentlyVending);

        // Load shelf i
        setAllLEDs(false);               // Turn off all LEDs
        setLED(currentlyVending, true); // Turn on LED for the selected shelf
      
        if (currentlyVending == 0){
          m1.attach(motorPins[currentlyVending]);
          m1.write(motorSpeed);               // Move motor to initial position
        } 
        else if (currentlyVending == 1){
          m2.attach(motorPins[currentlyVending]);
          m2.write(motorSpeed);               // Move motor to initial position
        }
        else if (currentlyVending == 2){
          m3.attach(motorPins[currentlyVending]);
          m3.write(motorSpeed);               // Move motor to initial position
        }
        else if (currentlyVending == 3){
          m4.attach(motorPins[currentlyVending]);
          m4.write(motorSpeed);               // Move motor to initial position
        }
        else if (currentlyVending == 4){
          m5.attach(motorPins[currentlyVending]);
          m5.write(motorSpeed);               // Move motor to initial position
        }
        else if (currentlyVending == 5){
          m6.attach(motorPins[currentlyVending]);
          m6.write(motorSpeed);               // Move motor to initial position
        }
}

void startLoad(){
        //Serial.println("Start Load");
        // Check if done vending
        if (getNextVendIndex() < 0) {
          state = CONNECTED;
          return;
        };
        
        // Check if button pressed
        if (retractionDetected()){
          state = CONNECTED;
          return;
        }

        currentlyVending = getNextVendIndex();

        //Serial.println("Currentlyvending: ");
        //Serial.println(currentlyVending);

        lastShelfRetracted = currentlyVending;

        if (currentlyVending >= 0 && currentlyVending < NUM_SHELVES && shelfSelections[currentlyVending]) { // Ensure bounds and check selection
            // Load shelf i
            setAllLEDs(false);               // Turn off all LEDs
            setLED(currentlyVending, true); // Turn on LED for the selected shelf
          
            if (currentlyVending == 0){
              m1.attach(motorPins[currentlyVending]);
              m1.write(motorSpeedReverse);               // Move motor to initial position
            } 
            else if (currentlyVending == 1){
              m2.attach(motorPins[currentlyVending]);
              m2.write(motorSpeedReverse);               // Move motor to initial position
            }
            else if (currentlyVending == 2){
              m3.attach(motorPins[currentlyVending]);
              m3.write(motorSpeedReverse);               // Move motor to initial position
            }
            else if (currentlyVending == 3){
              m4.attach(motorPins[currentlyVending]);
              m4.write(motorSpeedReverse);               // Move motor to initial position
            }
            else if (currentlyVending == 4){
              m5.attach(motorPins[currentlyVending]);
              m5.write(motorSpeedReverse);               // Move motor to initial position
            }
            else if (currentlyVending == 5){
              m6.attach(motorPins[currentlyVending]);
              m6.write(motorSpeedReverse);               // Move motor to initial position
            }
        }
}

void startVend() {
        
        // Check if done vending
        if (getNextVendIndex() < 0) {
          state = CONNECTED;
          return;
        };

        // Check if beams obstructed
        if (fallDetected()){
          //sendCommand("REMOVEITEM");
          client.send("{\"type\":\"OBSTRUCTION\",\"device\":\"" + String(DEVICE_TOKEN) + "\"}");
          state = BEAM_OBSTRUCTED;
          return;
        }
        vendStartTime = millis();
        currentlyVending = getNextVendIndex();
        if (currentlyVending >= 0 && currentlyVending < NUM_SHELVES && shelfSelections[currentlyVending]) { // Ensure bounds and check selection
            
            setAllLEDs(false);               // Turn off all LEDs
            setLED(currentlyVending, true); // Turn on LED for the selected shelf
          
            if (currentlyVending == 0){
              m1.attach(motorPins[currentlyVending]);
              m1.write(motorSpeed);               // Move motor to initial position
            } 
            else if (currentlyVending == 1){
              m2.attach(motorPins[currentlyVending]);
              m2.write(motorSpeed);               // Move motor to initial position
            }
            else if (currentlyVending == 2){
              m3.attach(motorPins[currentlyVending]);
              m3.write(motorSpeed);               // Move motor to initial position
            }
            else if (currentlyVending == 3){
              m4.attach(motorPins[currentlyVending]);
              m4.write(motorSpeed);               // Move motor to initial position
            }
            else if (currentlyVending == 4){
              m5.attach(motorPins[currentlyVending]);
              m5.write(motorSpeed);               // Move motor to initial position
            }
            else if (currentlyVending == 5){
              m6.attach(motorPins[currentlyVending]);
              m6.write(motorSpeed);               // Move motor to initial position
            }
        }
}

void stopBounce(){
        currentlyVending = lastShelfRetracted;

        if (currentlyVending == 0){
          m1.write(90);               // Reset motor to neutral position
          digitalWrite(motorPins[currentlyVending], LOW);
          m1.detach();
        } 
        else if (currentlyVending == 1){
          m2.write(90);               // Reset motor to neutral position
          digitalWrite(motorPins[currentlyVending], LOW);
          m2.detach();
        }
        else if (currentlyVending == 2){
          m3.write(90);               // Reset motor to neutral position
          digitalWrite(motorPins[currentlyVending], LOW);
          m3.detach();
        }
        else if (currentlyVending == 3){
          m4.write(90);               // Reset motor to neutral position
          digitalWrite(motorPins[currentlyVending], LOW);
          m4.detach();
        }
        else if (currentlyVending == 4){
          m5.write(90);               // Reset motor to neutral position
          digitalWrite(motorPins[currentlyVending], LOW);
          m5.detach();
        }
        else if (currentlyVending == 5){
          m6.write(90);               // Reset motor to neutral position
          digitalWrite(motorPins[currentlyVending], LOW);
          m6.detach();
        }

        // Wait for the motor to stabilize at neutral position
        delay(500);  // Allow motor time to settle
        shelfSelections[currentlyVending] = false;    // Deselect shelf after vending

        
        currentlyVending = -1;
        lastShelfRetracted = -1;
}

void stopLoad(){
        if (currentlyVending >= 0 && currentlyVending < NUM_SHELVES && shelfSelections[currentlyVending]) { // Ensure bounds and check selection
            if (currentlyVending == 0){
              m1.write(90);               // Reset motor to neutral position
              digitalWrite(motorPins[currentlyVending], LOW);
              m1.detach();
            } 
            else if (currentlyVending == 1){
              m2.write(90);               // Reset motor to neutral position
              digitalWrite(motorPins[currentlyVending], LOW);
              m2.detach();
            }
            else if (currentlyVending == 2){
              m3.write(90);               // Reset motor to neutral position
              digitalWrite(motorPins[currentlyVending], LOW);
              m3.detach();
            }
            else if (currentlyVending == 3){
              m4.write(90);               // Reset motor to neutral position
              digitalWrite(motorPins[currentlyVending], LOW);
              m4.detach();
            }
            else if (currentlyVending == 4){
              m5.write(90);               // Reset motor to neutral position
              digitalWrite(motorPins[currentlyVending], LOW);
              m5.detach();
            }
            else if (currentlyVending == 5){
              m6.write(90);               // Reset motor to neutral position
              digitalWrite(motorPins[currentlyVending], LOW);
              m6.detach();
            }

            // Wait for the motor to stabilize at neutral position
            delay(500);  // Allow motor time to settle
            shelfSelections[currentlyVending] = false;    // Deselect shelf after vending

        }
        currentlyVending = -1;
}

void stopVend(){
        if (currentlyVending >= 0 && currentlyVending < NUM_SHELVES && shelfSelections[currentlyVending]) { // Ensure bounds and check selection
            if (currentlyVending == 0){
              m1.write(90);               // Reset motor to neutral position
              digitalWrite(motorPins[currentlyVending], LOW);
              m1.detach();
            } 
            else if (currentlyVending == 1){
              m2.write(90);               // Reset motor to neutral position
              digitalWrite(motorPins[currentlyVending], LOW);
              m2.detach();
            }
            else if (currentlyVending == 2){
              m3.write(90);               // Reset motor to neutral position
              digitalWrite(motorPins[currentlyVending], LOW);
              m3.detach();
            }
            else if (currentlyVending == 3){
              m4.write(90);               // Reset motor to neutral position
              digitalWrite(motorPins[currentlyVending], LOW);
              m4.detach();
            }
            else if (currentlyVending == 4){
              m5.write(90);               // Reset motor to neutral position
              digitalWrite(motorPins[currentlyVending], LOW);
              m5.detach();
            }
            else if (currentlyVending == 5){
              m6.write(90);               // Reset motor to neutral position
              digitalWrite(motorPins[currentlyVending], LOW);
              m6.detach();
            }

            // Wait for the motor to stabilize at neutral position
            delay(500);  // Allow motor time to settle
            shelfSelections[currentlyVending] = false;    // Deselect shelf after vending

        }
        currentlyVending = -1;
        startVend();
}

int getNextVendIndex(){
    for (int y = 0; y < NUM_SHELVES ; y++){
      if (shelfSelections[y]){
        return y;
      }
    }
    return -1;
}

// void load(){
//   // if (!motor.attached()){
//   //   motor.attach(motorPin);
//   // }
//   // motorValue = 130;
//   // motor.write(motorValue);
// }

void select(int shelf) {
    if (shelf >= 0 && shelf < 6) { // Ensure shelf index is valid
        clearSelection();
        //Serial.println("Select " + String(shelf)); // Correctly concatenate string and int
        lastSelectTime = millis();
        noInterrupts(); // Disable interrupts to avoid race conditions
        shelfSelections[shelf] = !shelfSelections[shelf];
        interrupts(); // Re-enable interrupts
    } else {
        Serial.println("Error: Invalid shelf index");
    }
}

void clearSelection(){
  for (int i = 0; i < NUM_SHELVES; i++){
        noInterrupts(); // Disable interrupts to avoid race conditions
        shelfSelections[i] = false;
        interrupts(); // Re-enable interrupts
  }
}

int productToShelfIndex(const String& product) {
    for (int i = 0; i < 6; i++) {
        if (products[i] == product) {
            return i;
        }
    }
    return -1;
}

void clearWifiConfig(){
    config.setup = false;
    //Serial.println("Clearing WIFI config");
    EEPROM.put(CONFIG_SETUP_ADDRESS, config);
    EEPROM.commit();
}

void loadWifiConfig(){
  EEPROM.get(CONFIG_SETUP_ADDRESS, config);
}

int charToInt(char digit) {
  // Check if the input is a valid digit
  if (digit >= '0' && digit <= '9') {
    return digit - '0'; // Convert char to int
  } else {
    return -1; // Return -1 for invalid input
  }
}

void onMessageCallback(WebsocketsMessage message) {

  JsonDocument doc;
  deserializeJson(doc, message.data());
  
  String type = doc["type"];
  
  if (type == "payment_confirmed") {
    String item = doc["product"];
    int index = productToShelfIndex(doc["product"]);
    select(index);
    state = VENDING;
    startVend();
    Serial.println("💰 Payment received!");
    //onPaymentReceived(item);
  }
  else if (type == "hello") {
    Serial.println("✅ Connected to trigr");
    setAllLEDs(true);
    setScrollText(defaultScrollText);
  }
  else if (type == "ping") {
    client.send("{\"type\":\"PONG\",\"device\":\"" + String(DEVICE_TOKEN) + "\"}");
  }
  else if (type == "pin"){
    //setPin(doc["pin"], doc["expiresIn"]);
    pin = doc["pin"].as<String>();
    pinExpiryTime = millis() + doc["expiresIn"].as<String>().toInt();
    displayPin();
  }
  else if (type == "select"){
    Serial.println("🎯 Product selected: " + doc["product"].as<String>());
    int index = productToShelfIndex(doc["product"]);
    select(index);
  }
  else if (type == "load"){
    //select(charToInt(data[1])); NEED TO GET PRODUCTID -> INDEX FUNCTION
    state = LOADING;
    startLoad();
  }


    // String data = message.data(); // Copy the message data
    // data.trim();                  // Remove any trailing/leading whitespace
    // Serial.println(data);

    // if (data[0] == 'V') {
    //     select(charToInt(data[1]));
    //     state = VENDING;
    //     startVend(); // Assuming params contains a single digit
    // }
    // if (data[0] == 'I') {
    //     select(charToInt(data[1])); // Assuming params contains a single character
    // }
    // if (data[0] == 'L') {
    //     select(charToInt(data[1]));
    //     state = LOADING;
    //     startLoad();
    // }
    // if (data[0] == 'R') {
    //     clearWifiConfig();
    // }
    // if (data[0] == 'H') {
    //     //Serial.println("Received handshake");
    //     setAllLEDs(true);
    // }
    // if (data[0] == 'P'){
    //   Serial.println(data.substring(1));
    //   pin = data.substring(1);
    //   //extractExpiry(data);
    //   displayPin();
    // }
    // if (data[0] == 'E'){
    //   pinExpiryTime = millis() + data.substring(1).toInt();
    //   Serial.println(pinExpiryTime);
    // }
    // if (data[0] == 'U'){
    //   performOTA(data.substring(1));
    // }
}

void onEventsCallback(WebsocketsEvent event, String data) {
    if (event == WebsocketsEvent::ConnectionOpened) {
       // Serial.println("WebSocket connection opened.");
    } else if (event == WebsocketsEvent::ConnectionClosed) {
       // Serial.println("WebSocket connection closed.");
        state = RECONNECTING;
        connectToSocketServer();
    } else if (event == WebsocketsEvent::GotPing) {
        //Serial.println("Ping received.");
    } else if (event == WebsocketsEvent::GotPong) {
        //Serial.println("Pong received.");
        lastPongTime = millis();  // Reset timer to prevent immediate reconnect loops
    }
}

void WiFiEvent(WiFiEvent_t event) {
  // switch(event) {
  //   case ARDUINO_EVENT_WIFI_STA_CONNECTED:
  //     //Serial.println("Wi-Fi connected");
  //     state = RECONNECTING;
  //     connectToSocketServer();
  //     break;
  //   case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
  //     //Serial.println("Wi-Fi disconnected");
  //     state = WIFICONNECTFAILURE;
  //     break;
  //   case ARDUINO_EVENT_WIFI_STA_GOT_IP:
  //     //Serial.print("Got IP: ");
  //     //Serial.println(WiFi.localIP());
  //     break;
  //   default:
  //     break;
  // }
}

void blinkPin(int pin){
    static unsigned long lastSendTime = 0;
    if (millis() - lastSendTime > blinkInterval) {
        digitalWrite(pin, !digitalRead(pin));
        lastSendTime = millis();
    }
}

void blinkSelection(){
    unsigned long currentMillis = millis();
    
    if (currentMillis - previousMillis >= selectionBlinkInterval) {
        previousMillis = currentMillis; // Update the time
        ledState = !ledState;
      for (int i = 0; i < NUM_SHELVES; i++) {
            if (shelfSelections[i]) { // Check if the shelf is selected
                       // Toggle LED state
                setLED(i, ledState ? true : false);
            } else {
                // Ensure the LED is off if the shelf is not selected
                setLED(i, true);
            }
        }
    }
}

bool fallDetected(){

    if (digitalRead(BBPin) == LOW){
      //Serial.println("Fall Detected");
      //Serial.println(i);
      return true;
    }
    return false;
}

void connectToWifi(const char* ssid, const char* password) {
    Serial.print("Connecting to Wi-Fi");
    WiFi.begin(wifiSSID, wifipassword);

    unsigned long startTime = millis(); // Record the start time
    const unsigned long timeout = 10000; // 10 seconds timeout

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");

        // Check if timeout has occurred
        if (millis() - startTime >= timeout) {
            handleWifiTimeout(); // Call an alternate code path
            return;
        }
    }

    //Serial.println("\nConnected to Wi-Fi");

    // Set up WebSocket client
    client.onMessage(onMessageCallback);
    client.onEvent(onEventsCallback);

    delay(1000);

    connectToSocketServer();

}

void connectToSocketServer(){

    // Connect to production WebSocket
    if (PRODUCTION == true){

      client.setCACert(ssl_ca_cert);
      //Serial.println("Connecting to production server...");

      if (!client.connect(websocket_url_prod)) {
          Serial.println("Failed to connect to WebSocket server.");
          state = RECONNECTING;
          setAllLEDs(false);

          return;
      }
    }

    // Connect to local WebSocket
    else {
      client.setCACert(ssl_ca_cert);
      //Serial.println("Connecting to staging server...");
        if (!client.connect(websocket_url)) {
           // Serial.println("Failed to connect to WebSocket server.");
            state = RECONNECTING;
            setAllLEDs(false);

            return;
        }
    }

    Serial.println("Connected to WebSocket server.");
    state = CONNECTED;

    // machineId: string,
    // source: "MACHINE" | "PORTAL",
    // token: string,
    // command: string,
    // params: any
    setScrollText("Connecting to network...");

    String auth = "{\"type\":\"CONNECT\",\"device\":\"" + String(DEVICE_TOKEN) + "\"}";
    client.send(auth);

    // const char* strings[] = {
    //   "{\"machineId\":\"",
    //   MID,
    //   "\",\"source\": \"MACHINE\" ",
    //   ", \"token\":\"",
    //   UID,
    //   "\", \"command\": \"CONNECT\"}"
    // };
    
    // if (concatenateStrings(strings, 6, messageBuffer, sizeof(messageBuffer))){
    //   client.send(messageBuffer);
    //   setScrollText("Hi, scan me for local art!");
    // }
    // else{
    //   //Serial.println("Failed to construct handshake");
    //   setScrollText("Failed to Connect");
    // }
}

void handleWifiTimeout() {
    Serial.println("\nWi-Fi connection timed out!");
    state = WIFICONNECTFAILURE;
    blinkInterval = 500;
    startBluetooth();
    //setupAP(); // Start Access Point as fallback
}

bool concatenateStrings(const char* strings[], int numStrings, char* buffer, size_t bufferSize) {
    size_t currentLength = 0; // Tracks the current length of the concatenated string

    // Iterate over all input strings
    for (int i = 0; i < numStrings; i++) {
        size_t stringLength = strlen(strings[i]);

        // Check if adding this string would exceed the buffer size
        if (currentLength + stringLength >= bufferSize) {
            return false; // Indicate buffer overflow
        }

        // Append the string to the buffer
        strcpy(buffer + currentLength, strings[i]);
        currentLength += stringLength;
    }

    return true; // Indicate success
}

// void sendCommand(const char* command){

//     const char* strings[] = {
//       "{\"machineId\":\"",
//       MID,
//       "\",\"source\": \"MACHINE\" ",
//       ",\"token\":\"",
//       UID,
//       "\", \"command\": \"",
//       command,
//       "\"}"
//     };


//     //   const char* strings[] = {
//     // "{\"machineID\":",
//     // MID,
//     // ", \"command\": \"",
//     // command,
//     // "\"}"
//     // };
//     if (concatenateStrings(strings, 8, messageBuffer, sizeof(messageBuffer))){
//       client.send(messageBuffer);
//     }
//     else{
//       //Serial.println("Failed to construct handshake");
//     }
// }

// void sendParamCommand(const char* command, const char* param){

//     const char* strings[] = {
//       "{\"machineId\":\"",
//       MID,
//       "\",\"source\": \"MACHINE\" ",
//       ",\"token\":\"",
//       UID,
//     "\", \"command\": \"",
//     command,
//     "\", \"params\": \"",
//     param,
//     "\"}"
//     };


//     //   const char* strings[] = {
//     // "{\"machineID\":",
//     // MID,
//     // ", \"command\": \"",
//     // command,
//     // "\", \"params\": \"",
//     // param,
//     // "\"}"
//     // };
//     if (concatenateStrings(strings, 10, messageBuffer, sizeof(messageBuffer))){
//       client.send(messageBuffer);
//     }
//     else{
//       Serial.println("Failed to construct handshake");
//     }
// }

// Setup and launch soft AP to expose config server
void setupAP(){
  WiFi.softAP("PopVendConfig", "12345678");  // Set AP credentials
  
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/html", htmlForm);
  });

  server.on("/get", HTTP_GET, [](AsyncWebServerRequest *request){
    String ssid = request->getParam("ssid")->value();
    String password = request->getParam("password")->value();
    Serial.println("SSID: " + ssid + " Password: " + password);

    // Write default config to EEPROM
    //delay(5000);
    strcpy(config.ssid, ssid.c_str());
    strcpy(config.password, password.c_str());
    config.setup = true;

    Serial.println("NEW WiFi configuration:");
    Serial.println(config.ssid);
    Serial.println(config.password);
    Serial.println(config.setup);
    

    EEPROM.put(CONFIG_SETUP_ADDRESS, config);
    EEPROM.commit();
  
    //Serial.println("Saved to eeprom");
    state = STARTING;
    delay(5000);
    ESP.restart();

  });

  server.begin();
}


// OLED Functions

void setScrollText(String text){
  oledScrollX = SCREEN_WIDTH;
  oledScrollText = text;
}

// Scroll a pre defined string across the screen
void scrollTextTick() {
  int textWidth = oledScrollText.length() * 6 * 4; // width in pixels for text size 1 (6px per char)
  if (oledScrollX + textWidth > 0) {
    display.clearDisplay();
    display.setTextSize(4);      // 1 = 6x8 font
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(oledScrollX, 0);     // y = 0
    display.println(oledScrollText);
    display.display();
    oledScrollX--;
  }
  else{
    oledScrollX = SCREEN_WIDTH;
  }
}

void displayPin(){
    oledScrollText = "";
    display.clearDisplay();
    display.setTextSize(4);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(20, 0); 
    display.println(pin);
    display.display();
}

void performOTA(String url) {

  Serial.println("Starting OTAU");
  Serial.println(url);

  WiFiClient client;
  HTTPClient http;

  http.begin(client, url);
  int httpCode = http.GET();

  Serial.println(httpCode);

  if (httpCode == HTTP_CODE_OK) {

    int contentLength = http.getSize();
    bool canBegin = Update.begin(contentLength);

    Serial.println("Can begin?");
    Serial.println(canBegin);

    if (canBegin) {

      WiFiClient * stream = http.getStreamPtr();
      size_t written = Update.writeStream(*stream);

      if (written == contentLength) {
        if (Update.end()) {
          if (Update.isFinished()) {
            ESP.restart();
          }
        }
      }
    }
  }

  http.end();

}

String extractJsonValue(const String& json, const String& key) {

  String search = "\"" + key + "\":\"";
  int start = json.indexOf(search);

  if (start == -1) return "";

  start += search.length();
  int end = json.indexOf("\"", start);

  if (end == -1) return "";

  return json.substring(start, end);
}

// Bluetooth Callbacks
class MyCallbacks : public BLECharacteristicCallbacks {

  void onWrite(BLECharacteristic *pCharacteristic) override {

    auto value = pCharacteristic->getValue();

    if (value.length() > 0) {
      Serial.println("Received:");
      String data = value.c_str();

      String ssid = extractJsonValue(data, "ssid");
      String password = extractJsonValue(data, "password");
      String uid = extractJsonValue(data, "uid");
      String id = extractJsonValue(data, "id");
    
      Serial.println(ssid);
      Serial.println(password);
      Serial.println(uid);
      Serial.println(id);

      strcpy(config.ssid, ssid.c_str());
      strcpy(config.password, password.c_str());
      strcpy(config.uid, uid.c_str());
      strcpy(config.id, id.c_str());
      config.setup = true;

      EEPROM.put(CONFIG_SETUP_ADDRESS, config);
      EEPROM.commit();

      restartRequested = true;

    }

  }

};

void startBluetooth(){

  Serial.println("Starting bluetooth server");

  BLEDevice::init("PopVend");
  BLEServer *pServer = BLEDevice::createServer();
  BLEService *pService = pServer->createService(SERVICE_UUID);

  BLECharacteristic *pCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_WRITE
  );

  pCharacteristic->setCallbacks(new MyCallbacks());

  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  BLEDevice::startAdvertising();

  Serial.println("BLE device is now advertising...");

}

