#include<Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <MFRC522.h>
#include<SPI.h>
#include<WiFi.h>
#include<PubSubClient.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>


#define LCD1_ROWS 4              
#define LCD1_COLNS 20

#define LCD2_ROWS 2
#define LCD2_COLNS 16

#define WIFI_SSID "Real Me" 
#define PASSWORD "123456789"
            
#define BROKER_HOST "skfplc.mqtt.vsensetech.in"
#define BROKER_PORT 1883
#define BROKER_USER_NAME ""
#define BROKER_PASSWORD ""
#define KEEP_ALIVE_INTERVAL 5
#define UNIT_ID "smart/controller/1"
#define SUBSCRIBE_TOPIC "smart/controller/1"
#define PUBLISH_TOPIC1 "smart/mqtt/processor/1"
#define PUBLISH_TOPIC2 "smart/mqtt/processor/2"
#define PUBLISH_TOPIC3 "smart/mqtt/processor/3"

#define SLOT1_IR_PIN 33
#define SLOT2_IR_PIN 32
#define SLOT3_IR_PIN 35
#define SLOT4_IR_PIN 34

#define GATE1_SERVO_PIN 26
#define GATE1_IR_PIN 27

#define GATE2_SERVO_PIN 13
#define GATE1_SENSOR_INDICATION_LED_PIN 17
  
#define SS_PIN 5
#define RST_PIN 0

#define STABILITY_COUNT 3


uint8_t slot1PrevStatus = 0;
uint8_t slot2PrevStatus = 0;
uint8_t slot3PrevStatus = 0;
uint8_t slot4PrevStatus = 0;

uint8_t stableSlot1SensorStatus = 1;
uint8_t stableSlot2SensorStatus = 1;
uint8_t stableSlot3SensorStatus = 1;
uint8_t stableSlot4SensorStatus = 1;

uint8_t stableGate1SensorStatus = 1;

uint8_t slot1SensorStabilityCounter = 0;
uint8_t slot2SensorStabilityCounter = 0;
uint8_t slot3SensorStabilityCounter = 0;
uint8_t slot4SensorStabilityCounter = 0;

uint8_t gate1SensorStabilityCounter = 0;

uint8_t gate1SensorStatePublished = 0;
uint8_t rfidStatePublished = 0;

uint8_t gate1StatePublishedResetCounter = 0;
uint8_t rfidStatePublishedResetCounter = 0;

//lcd
LiquidCrystal_I2C lcd1(0x27,LCD1_COLNS,LCD1_ROWS);
LiquidCrystal_I2C lcd2(0x26,LCD1_COLNS,LCD2_ROWS);

//rfid
MFRC522 rfid(SS_PIN,RST_PIN);

//tcp client
WiFiClient tcp;

PubSubClient mqtt(tcp);

Servo gate1Servo;
Servo gate2Servo;

JsonDocument doc;


void setup() {
  Serial.begin(9600);
  //IR sensor pins
  pinMode(SLOT1_IR_PIN,INPUT);
  pinMode(SLOT2_IR_PIN,INPUT);
  pinMode(SLOT3_IR_PIN,INPUT);
  pinMode(SLOT4_IR_PIN,INPUT);
  pinMode(GATE1_IR_PIN,INPUT);
  pinMode(GATE1_SENSOR_INDICATION_LED_PIN,OUTPUT);

  //spi
  SPI.begin();

  //rfid
  rfid.PCD_Init();

  //servo
  gate1Servo.attach(GATE1_SERVO_PIN);
  gate2Servo.attach(GATE2_SERVO_PIN);

  //initializing lcd
  lcd1.init();
  lcd1.clear();
  lcd1.backlight();

  lcd2.init();
  lcd2.clear();
  lcd2.backlight();

  //servo
  gate1Servo.write(0);

  //initializing wifi
  WiFi.mode(WIFI_STA);

  connectToWiFi();
  connectToBroker();

}

void loop() {
  if(WiFi.status() != WL_CONNECTED) {
    connectToWiFi();
  }

  if(WiFi.status() == WL_CONNECTED && !mqtt.connected()) {
    connectToBroker();
  }

  publishSensorStatusToBroker();

  mqtt.loop();

  //check gate1
  checkGate1();

  //check for rfid
  checkRfid();

  if(gate1SensorStatePublished  && gate1StatePublishedResetCounter > 100) {
    gate1SensorStatePublished=0;
    gate1StatePublishedResetCounter=0;
  }

  if(gate1SensorStatePublished) {
    gate1StatePublishedResetCounter++;
  }

  if(rfidStatePublished && rfidStatePublishedResetCounter > 100 ) {
    rfidStatePublished=0;
    rfidStatePublishedResetCounter=0;
  }

  if(rfidStatePublished) {
    rfidStatePublishedResetCounter++;
  }


  vTaskDelay(pdMS_TO_TICKS(100));
}


void connectToWiFi() {

  uint8_t wifiTimeOutCounter = 10; 

  WiFi.disconnect();

  WiFi.begin(WIFI_SSID,PASSWORD);

  while(WiFi.status() != WL_CONNECTED && wifiTimeOutCounter > 0 ) {
    Serial.printf("connecting to wifi -> %u\n",wifiTimeOutCounter);
    wifiTimeOutCounter--;
    delay(1000);
  } 

  if(WiFi.status() == WL_CONNECTED) {
    Serial.printf("connected to wifi\n");
  } else {
    Serial.printf("failed to connect wifi\n");
  }
}

void connectToBroker(){
  if(WiFi.status() != WL_CONNECTED) {
    return;
  }

  uint8_t mqttTimeOutCounter = 10;

  mqtt.setServer(BROKER_HOST,BROKER_PORT);
  mqtt.setCallback(mqttMessageHandler);
  mqtt.setKeepAlive(KEEP_ALIVE_INTERVAL);

  while(!mqtt.connected() && mqttTimeOutCounter > 0 ) { 
    mqtt.connect(UNIT_ID);
    if(mqtt.connected()) {
      Serial.printf("connected to broker\n");
      mqtt.subscribe(SUBSCRIBE_TOPIC);
      return;
    }
    mqttTimeOutCounter--;
   
  }
}

void mqttMessageHandler(char *topic,byte *payload, unsigned int length) {
  String message((char *)payload,length);
  // Serial.println(message);
  doc.clear();
  DeserializationError err = deserializeJson(doc,message.c_str());

  if(err) {
    Serial.printf("error occurred while decoding json message");
    return;
  }

  uint8_t messageType = doc["mty"];

  String slotEmpty = "EMPTY";
  String slotFull = "FULL";
  String slotBooked = "BOOKED";

  if(messageType == 1) {

    uint8_t slot1Status = doc["s1"];
    uint8_t slot2Status = doc["s2"];
    uint8_t slot3Status = doc["s3"];
    uint8_t slot4Status = doc["s4"];

    if(slot1PrevStatus != slot1Status ||slot2PrevStatus != slot2Status || slot3PrevStatus != slot3Status ||slot4PrevStatus != slot4Status ) {
      displaySlotStatus(
        slot1Status == 2 ? slotBooked : ( slot1Status == 1? slotFull : slotEmpty),
        slot2Status == 2 ? slotBooked : ( slot2Status == 1? slotFull : slotEmpty),
        slot3Status == 2 ? slotBooked : ( slot3Status == 1? slotFull : slotEmpty),
        slot4Status == 2 ? slotBooked : ( slot4Status == 1? slotFull : slotEmpty)
      );

      slot1PrevStatus = slot1Status;
      slot2PrevStatus = slot2Status;
      slot3PrevStatus = slot3Status;
      slot4PrevStatus = slot4Status;
    }

  } else if(messageType == 2) {
    uint8_t gateOpenStatus = doc["gos"];
    if(gateOpenStatus == 1) {
      String slotId = doc["sid"];
      displaySlotGuidingInfo(slotId);
      delay(2000);
      openGate1();
      delay(4000);
      closeGate1();
    } else {
      displaySlotFullMessage();
      delay(3000);
    }

    String slotEmpty = "EMPTY";
    String slotFull = "FULL";
    String slotBooked = "BOOKED";

    displaySlotStatus(
      slot1PrevStatus == 2 ? slotBooked : ( slot1PrevStatus == 1? slotFull : slotEmpty),
      slot2PrevStatus == 2 ? slotBooked : ( slot2PrevStatus == 1? slotFull : slotEmpty),
      slot3PrevStatus == 2 ? slotBooked : ( slot3PrevStatus == 1? slotFull : slotEmpty),
      slot4PrevStatus == 2 ? slotBooked : ( slot4PrevStatus == 1? slotFull : slotEmpty)
    );

    gate1SensorStatePublished=0;
    gate1StatePublishedResetCounter=0;
  } else if(messageType == 3) {
    uint cost = doc["cost"];
    displaySlotUsageAmount(String(cost));

    delay(3000);

    openGate2();

    delay(4000);

    closeGate2();

    rfidStatePublished=0;
    rfidStatePublishedResetCounter=0;
  } else if(messageType == 4) {
    uint8_t gate1SensorReading = digitalRead(GATE1_IR_PIN);

    if (!gate1SensorReading) {
      openGate1();
      delay(4000);
      closeGate1();
    }
  }
}

void displaySlotStatus(String& slot1Status, String& slot2Status, String& slot3Status, String& slot4Status){
    lcd1.clear();
    lcd1.setCursor(1, 0);
    lcd1.print("S1:"+slot1Status);

    lcd1.setCursor(11,0);
    lcd1.print("S2:"+slot2Status);

    lcd1.setCursor(1,3);
    lcd1.print("S3:"+slot3Status);

    lcd1.setCursor(11, 3);
    lcd1.print("S4:"+slot4Status);    
}

void displaySlotGuidingInfo(String slotId) {
    lcd1.clear();
    lcd1.setCursor(6, 0);
    lcd1.print("WELL COME");

    lcd1.setCursor(2, 2);
    lcd1.print("GO TO SLOT ID: "+slotId);
}

void displaySlotFullMessage() {
    lcd1.clear();
    lcd1.setCursor(6, 0);
    lcd1.print("SORRY!!");
    lcd1.setCursor(0, 2);
    lcd1.print("PARKING SLOT IS FULL");
}

void displaySlotUsageAmount(String amount) {
  lcd2.clear();
  lcd2.setCursor(1,0);
  lcd2.print("USAGE AMT: "+amount);
}



void publishSensorStatusToBroker() {

  uint8_t slot1SensorCurrentStatus = digitalRead(SLOT1_IR_PIN);
  uint8_t slot2SensorCurrentStatus = digitalRead(SLOT2_IR_PIN);
  uint8_t slot3SensorCurrentStatus = digitalRead(SLOT3_IR_PIN);
  uint8_t slot4SensorCurrentStatus = digitalRead(SLOT4_IR_PIN);

  if(slot1SensorCurrentStatus == stableSlot1SensorStatus) {
    slot1SensorStabilityCounter++; 
  } else {
    slot1SensorStabilityCounter=0;
    stableSlot1SensorStatus=slot1SensorCurrentStatus;
  }

  if(slot2SensorCurrentStatus == stableSlot2SensorStatus) {
    slot2SensorStabilityCounter++; 
  } else {
    slot2SensorStabilityCounter=0;
    stableSlot2SensorStatus=slot2SensorCurrentStatus;
  }

  if(slot3SensorCurrentStatus == stableSlot3SensorStatus) {
    slot3SensorStabilityCounter++; 
  } else {
    slot3SensorStabilityCounter=0;
    stableSlot3SensorStatus=slot3SensorCurrentStatus;
  }

  if(slot4SensorCurrentStatus == stableSlot4SensorStatus) {
    slot4SensorStabilityCounter++; 
  } else {
    slot4SensorStabilityCounter=0;
    stableSlot4SensorStatus=slot4SensorCurrentStatus;
  }

  if( slot1SensorStabilityCounter >= STABILITY_COUNT &&
      slot2SensorStabilityCounter >= STABILITY_COUNT &&
      slot2SensorStabilityCounter >= STABILITY_COUNT &&
      slot2SensorStabilityCounter >= STABILITY_COUNT 
    ) {

      doc.clear();

      doc["s1"] = stableSlot1SensorStatus ? 0 : 1;
      doc["s2"] = stableSlot2SensorStatus ? 0 : 1;
      doc["s3"] = stableSlot3SensorStatus ? 0 : 1;
      doc["s4"] = stableSlot4SensorStatus ? 0 : 1;

      String jsonMessage;

      serializeJson(doc,jsonMessage);

      mqtt.publish(PUBLISH_TOPIC1,jsonMessage.c_str());
    
      slot1SensorStabilityCounter = 0;
      slot2SensorStabilityCounter = 0;
      slot3SensorStabilityCounter = 0;
      slot4SensorStabilityCounter = 0;
  
    }
}

void checkGate1() {
   uint8_t gate1SensorCurrentStatus = digitalRead(GATE1_IR_PIN);

   if(!gate1SensorCurrentStatus) {
    turnOnGate1SensorIndicationLed();
   } else {
    turnOffGate1SensorIndicationLed();
   }

   if(gate1SensorCurrentStatus == stableGate1SensorStatus) {
      gate1SensorStabilityCounter++;
   } else {
      gate1SensorStabilityCounter=0;
      stableGate1SensorStatus = gate1SensorCurrentStatus;
   }

   if(gate1SensorStabilityCounter >= STABILITY_COUNT && !stableGate1SensorStatus && !gate1SensorStatePublished) {
      doc.clear();
    
      String jsonMessage;
      serializeJson(doc,jsonMessage);

      mqtt.publish(PUBLISH_TOPIC2,jsonMessage.c_str());

      gate1SensorStatePublished=1;
   }
}

void checkRfid() {
    if(!rfid.PICC_IsNewCardPresent()) {
      return;
    }

    if(!rfid.PICC_ReadCardSerial()) {
      return;
    }

    String slotRfid ="";

   for(uint8_t i=0; i<rfid.uid.size; i++) {
    slotRfid += String(rfid.uid.uidByte[i],HEX);
   }


  if(!rfidStatePublished) {

    doc.clear();

    doc["rfid"]=slotRfid;

    String jsonMessage;

    serializeJson(doc,jsonMessage);

    mqtt.publish(PUBLISH_TOPIC3,jsonMessage.c_str());

    rfidStatePublished=1;
  }


  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

void openGate1() {
  gate1Servo.write(135);
}

void closeGate1() {
  gate1Servo.write(0);
}

void openGate2() {
  gate2Servo.write(135);
}

void closeGate2() {
  gate2Servo.write(0);
}

void turnOnGate1SensorIndicationLed() {
  digitalWrite(GATE1_SENSOR_INDICATION_LED_PIN,1);
}

void turnOffGate1SensorIndicationLed() {
  digitalWrite(GATE1_SENSOR_INDICATION_LED_PIN,0);
}