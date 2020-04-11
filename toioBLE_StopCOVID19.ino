#include <M5Stack.h>
#include <BLEDevice.h>
#include <Adafruit_NeoPixel.h>

unsigned long tickTime = millis();

// toio自体, モーターなどをBLE Peripheralとして発見するためにService UUIDを設定
// M5側がtoioのadvertiseを受けて検出できる
// toio service
static BLEUUID serviceUUID("10B20100-5B3B-4571-9508-CF3EFCD7BBAE");
// motor characteristic
static BLEUUID motor_charUUID("10B20102-5B3B-4571-9508-CF3EFCD7BBAE");
// sound characteristic
static BLEUUID sound_charUUID("10B20104-5B3B-4571-9508-CF3EFCD7BBAE");

static BLEAdvertisedDevice* targetDevice;
static BLEClient*  pClient;
static BLERemoteCharacteristic* sound_characteristic;
static boolean doConnect = false;
static boolean doScan = false;

static boolean is_req_lightup = false;

bool fConnected = false;

// toioキューブの制御に必要な構成データ
static uint8_t sound_data_size = 9;
static uint8_t current_sound = 0;

// Ultrasonic SensorをM5Stackの21番ピンに接続
const int SigPin = 21;
long duration;
int distance;

#define PIN 22
#define FRAME_INTERVAL 50

#define NUM_LEDS 8
Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUM_LEDS, PIN, NEO_GRB + NEO_KHZ800);


/**
 * Scan for BLE servers and find the first one that advertises the service we are looking for.
 */
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertised_device) {
    if (advertised_device.haveServiceUUID() && advertised_device.isAdvertisingService(serviceUUID)) {
      Serial.println("[BLE] Found service");
      Serial.println("[BLE] Scan stop");
      BLEDevice::getScan()->stop();
      targetDevice = new BLEAdvertisedDevice(advertised_device);
      doConnect = true;
      doScan = true;
    } // Found our server
  } // onResult
}; // Myadvertised_deviceCallbacks

class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pClient) {
    fConnected = true;
    Serial.println("onConnect");
  }

  void onDisconnect(BLEClient* pClient) {
    fConnected = false;
    Serial.println("onDisconnect");
  }
};

// 超音波センサーで10cm以内に近づいたら光る
static void lightLED(void)
{
  static bool is_near = false;
  is_near = distance;
  if (distance < 10){
    M5.Lcd.drawString("MOVE", 0, 180);
    is_req_lightup = true;
    colorWipe(strip.Color(255, 0, 0), 50); // Red
  }else{
    M5.Lcd.drawString("STOP", 0, 180);
    is_req_lightup = false;
    colorWipe(strip.Color(0, 0, 0), 50); // Black
  }
}

static void sendSoundControl(void)
{
  uint8_t data[sound_data_size] = {0};
  data[0] = 0x03; //0x03:MIDI note number
  data[1] = 0x05; //繰り返しの回数 5回
  data[2] = (is_req_lightup ? 0x02 : 0x0); //Operationの数 2つ
  data[3] = 0x1E; //再生時間 300ミリ秒
  data[4] = 0x3C; //MIDI note number C5
  data[5] = 0xFF; //音量 最大
  data[6] = 0x1E; //再生時間 300ミリ秒
  data[7] = 0x40; //MIDI note number E5
  data[8] = 0xFF; //音量 最大

  sound_characteristic->writeValue(data, sizeof(data));
}

bool connectToServer() {
    pClient  = BLEDevice::createClient();    
//    BLEClient*  pClient  = BLEDevice::createClient();
    Serial.println(" - Created client");

    pClient->setClientCallbacks(new MyClientCallback());

    // Connect to the remove BLE Server.
    pClient->connect(targetDevice);  // if you pass BLEadvertised_device instead of address, it will be recognized type of peer device address (public or private)
    Serial.println(" - Connected to server");

    // Obtain a reference to the service we are after in the remote BLE server.
    BLERemoteService* remote_service = pClient->getService(serviceUUID);
    if (remote_service == NULL) {
      Serial.print("Failed to find our service UUID: ");
      Serial.println(serviceUUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    fConnected = true;
    Serial.println(" - Found toio service");
    Serial.println(uxTaskGetStackHighWaterMark(NULL));

    //sound
    sound_characteristic = remote_service->getCharacteristic(sound_charUUID);
    if (sound_characteristic == NULL) {
      Serial.print("Failed to find sound characteristic UUID: ");
      Serial.println(sound_charUUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println(" - Found sound characteristic");
    
    return true;
//    fConnected = true;
}

void setup() {
  //初期設定
  M5.begin();
  M5.Lcd.fillScreen(0x0000);
  M5.Lcd.setTextFont(2);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(0xFFFF, 0x0000);

  Serial.print(115200);
  M5.Lcd.println("[BLE] Start");
  BLEDevice::init("");

  // Retrieve a Scanner and set the callback we want to use to be informed when we
  // have detected a new device.  Specify that we want active scanning and start the
  // scan to run for 5 seconds.
  BLEScan* ble_scan = BLEDevice::getScan();
  ble_scan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  ble_scan->setInterval(1349);
  ble_scan->setWindow(449);
  ble_scan->setActiveScan(true);
  ble_scan->start(5, false);

  // 測距センサーに接続したピンのモードを出力にセット
  pinMode(SigPin, OUTPUT);
  digitalWrite(SigPin, LOW);

  strip.begin();
  strip.show(); // Initialize all pixels to 'off'
  strip.setBrightness(20);

} // End of setup.

// This is the Arduino main loop function.
void loop() {

  M5.update();

  // 超音波センサー
  pinMode(SigPin, OUTPUT);
  digitalWrite(SigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(SigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(SigPin, LOW);

  pinMode(SigPin, INPUT);
  duration = pulseIn(SigPin, HIGH);
  distance= duration*0.034/2;
  Serial.println("Distance: " + String(distance) + " cm");
  delay(2000);
  
  // If the flag "doConnect" is true then we have scanned for and found the desired
  // BLE Server with which we wish to connect.  Now we connect to it.  Once we are 
  // connected we set the connected flag to be true.
  if (doConnect == true) {
    if (connectToServer()) {
      M5.Lcd.println("[BLE] Connect");
    } else {
      M5.Lcd.println("[BLE] Error on connect");
    }
    doConnect = false;
  }

 // If we are connected to a peer BLE Server, update the characteristic each time we are reached
 // with the current time since boot.
  if (fConnected) {
    lightLED();
    sendSoundControl();
  }else if(doScan){
    BLEDevice::getScan()->start(0);  // this is just eample to start scan after disconnect, most likely there is better way to do it in arduino
  }

    if(M5.BtnA.wasPressed()){
    colorWipe(strip.Color(255, 0, 0), 50); // Red
    colorWipe(strip.Color(0, 255, 0), 50); // Green
    colorWipe(strip.Color(0, 0, 255), 50); // Blue
  }
  else if(M5.BtnB.wasPressed()){
    rainbow(20);
  }
  else if(M5.BtnC.wasPressed()){
    colorWipe(strip.Color(0, 0, 0), 50); // Black
  }

  delay(1000); // Delay a second between loops.
} // End of loop

// Fill the dots one after the other with a color
void colorWipe(uint32_t c, uint8_t wait) {
  for(uint16_t i=0; i<strip.numPixels(); i++) {
    strip.setPixelColor(i, c);
    strip.show();
    delay(wait);
  }
}

void rainbow(uint8_t wait) {
  uint16_t i, j;

  for(j=0; j<256; j++) {
    for(i=0; i<strip.numPixels(); i++) {
      strip.setPixelColor(i, Wheel((i+j) & 255));
    }
    strip.show();
    delay(wait);
  }
}

// Slightly different, this makes the rainbow equally distributed throughout
void rainbowCycle(uint8_t wait) {
  uint16_t i, j;

  for(j=0; j<256*5; j++) { // 5 cycles of all colors on wheel
    for(i=0; i< strip.numPixels(); i++) {
      strip.setPixelColor(i, Wheel(((i * 256 / strip.numPixels()) + j) & 255));
    }
    strip.show();
    delay(wait);
  }
}

//Theatre-style crawling lights.
void theaterChase(uint32_t c, uint8_t wait) {
  for (int j=0; j<10; j++) {  //do 10 cycles of chasing
    for (int q=0; q < 3; q++) {
      for (uint16_t i=0; i < strip.numPixels(); i=i+3) {
        strip.setPixelColor(i+q, c);    //turn every third pixel on
      }
      strip.show();

      delay(wait);

      for (uint16_t i=0; i < strip.numPixels(); i=i+3) {
        strip.setPixelColor(i+q, 0);        //turn every third pixel off
      }
    }
  }
}

//Theatre-style crawling lights with rainbow effect
void theaterChaseRainbow(uint8_t wait) {
  for (int j=0; j < 256; j++) {     // cycle all 256 colors in the wheel
    for (int q=0; q < 3; q++) {
      for (uint16_t i=0; i < strip.numPixels(); i=i+3) {
        strip.setPixelColor(i+q, Wheel( (i+j) % 255));    //turn every third pixel on
      }
      strip.show();

      delay(wait);

      for (uint16_t i=0; i < strip.numPixels(); i=i+3) {
        strip.setPixelColor(i+q, 0);        //turn every third pixel off
      }
    }
  }
}

// Input a value 0 to 255 to get a color value.
// The colours are a transition r - g - b - back to r.
uint32_t Wheel(byte WheelPos) {
  WheelPos = 255 - WheelPos;
  if(WheelPos < 85) {
    return strip.Color(255 - WheelPos * 3, 0, WheelPos * 3);
  }
  if(WheelPos < 170) {
    WheelPos -= 85;
    return strip.Color(0, WheelPos * 3, 255 - WheelPos * 3);
  }
  WheelPos -= 170;
  return strip.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
}
