/*
 * Handshake & random ping
 */

/**
 * proximity sensing by having random delay in sending
 */

#include <SPI.h>
#include <stdlib.h>
#include "nRF24L01.h"
#include "RF24.h"
#include "printf.h"
#include <SoftwareSerial.h>
#include <EEPROM.h>
#include <EEPROMAnything.h>
#include <TrueRandom.h>

// set up the imp
SoftwareSerial impSerial(8, 9);

// handshake and accelerometer thresholds
const int TIME_THRESHOLD_MIN = 100;
const int TIME_THRESHOLD_MAX = 500;
const int MIN_CROSSINGS = 4;
const int MAX_CROSSINGS = 40;
const int THRESHOLD = 3;
const int MAX_THRESHOLD = 5;
const int AVG_SIZE = 10; // average the first values for a baseline
const int DELAY_TIME = 1; 
const short ANALOG_PIN = 5; 
const short LED_PIN = 5;
const int LED_DELAY = 1000;

int led_count = -1;
boolean ledError = false;
long timestamp = 0;
long timestamps[MAX_CROSSINGS];
int timestamp_pos = 0;
int prev = 0;
int current = 0;
int avg = 0;

// UUID paring
boolean synced = false;
// check if paring is already done in EERPROM
char START_UP_SYNC_CODE[] = "PAIRED";
const int START_UP_SYNC_ADDRESS = 130;

/* EEPROM UUID
// START_UP_STRING_CODE is a code that we can check EEPROM for to see if we have 
// already saved a UID in EEPROM. We save this code to memory just after
// receiving the UID. The reason for it is 
// because our EEPROM isn't guaranteed to be uninitualized. Therefore,
// we can't just check a certain point in EEPROM memory for a UID
// because it might be trash. Instead, we will check the EEPROM for this
// code at a certain point in memory that basically tells us that 
// we have already saved a UID. 
*/
char START_UP_STRING_CODE[] = "ALREADY STORED";
// Where we are saving the code in EEPROM
const int START_UP_STRING_CODE_ADDRESS = 230;
// The first address that the UUID is saved at
const int START_UUID_ADDRESS = 250;
// The size, in bytes, of the UUID
const int UUID_SIZE = 8;
// The UUID itself. It can be loaded
byte UUID[UUID_SIZE];

byte receivedUUID[UUID_SIZE]; // blank UUID for the received message


// nRF24L01 setup on SPI bus plus pins 6 & 10
RF24 radio(6,10);
const short ROLE_PIN = 7;

boolean sendMessage = false;

// Single radio pipe address for all the nodes to communicate.
const uint64_t pipe = 0xF0F0F0F0E1LL;

void setup(void)
{ 
  // set up the accelerometer
  int avgPos = 0;
  while (avgPos < AVG_SIZE) {
    avgPos++;
    avg += analogRead(ANALOG_PIN);
    current = prev = analogRead(ANALOG_PIN);
    delay(DELAY_TIME);
  }
  avg = avg/AVG_SIZE;
  
  Serial.begin(57600);
  impSerial.begin(19200);
  printf_begin();
  Serial.println("Random Ping");

  // Setup and configure rf radio  
  radio.begin();
  radio.setRetries(15,15);
  radio.setPayloadSize(8);
  radio.setPALevel(RF24_PA_MIN);
  radio.setDataRate(RF24_250KBPS);
  radio.enableAckPayload();

  // open up the reading pipe
  radio.openReadingPipe(1,pipe);

  radio.startListening();
  Serial.println(avg);
  // Dump the configuration of the rf unit for debugging
  radio.printDetails();  
  
  // Uncomment the line below to clear the UUID
//    clearUUID();
  
  if (!isStoredEEPROM(START_UP_STRING_CODE, START_UP_STRING_CODE_ADDRESS)) { 
   Serial.println("No UUID Found. Creating one...");
   createNewUUID();
  } else {
   Serial.println("UUID Found: ");
   loadDeviceUUIDFromEEPROM();
   printUUID();
  }
  
  // Uncomment the line below to clear the sync
//  clearSync();
  if (!isStoredEEPROM(START_UP_SYNC_CODE, START_UP_SYNC_ADDRESS)){
    Serial.println("device is not synced");
    ledError = true;
  } else {
    synced = true;
    Serial.println("device already synced");
  }
}

/*
  Check if nRF24 has received any data. If it has, send it to the imp
  */
boolean checkReceive() {
   // if there is data
  if ( radio.available() )
  {
    bool finished = false;
    while (!finished)
    {
      finished = radio.read( &receivedUUID, UUID_SIZE );
      Serial.print("Sending received UUID to the imp: ");
      for (int i = 0; i < UUID_SIZE;i++) {
        Serial.print(receivedUUID[i], DEC);
      }
      Serial.println();
//      Serial.println(receivedUUID);
      Serial.println("That was the received UUID");
      impSerial.print("$");
      impSerial.print("{\"id\": \"");
      for (int i = 0; i < UUID_SIZE;i++) {
        impSerial.print(UUID[i], DEC);
      }
      impSerial.print("\", \"received\": \"");
      for (int i = 0; i < UUID_SIZE;i++) {
        impSerial.print(receivedUUID[i], DEC);
      }
//      impSerial.print(receivedUUID, DEC);
      impSerial.print("\"}");
      impSerial.println("#");
    }
  }
}

/*
  Check if values have crossed over baseline that's within threshold values
  */
boolean crossedAvg() {
  return (((prev < avg && current > avg) || (prev > avg && current < avg)) && 
    abs(current - prev) >= THRESHOLD && abs(current-prev) < MAX_THRESHOLD );
}

/* 
  Flashes the LED for LED_DELAY time if something has occured
  set led_count = 0 when this should go off
 */
void flash_led() {
  if ( (led_count != -1 || ledError) && led_count <= LED_DELAY) {
    ++led_count;
    digitalWrite(LED_PIN, HIGH);
  } else if ( led_count == -1 || led_count > LED_DELAY) {
    led_count = -1;
    digitalWrite(LED_PIN, LOW);
  }
}

/*
  resets the timestamp logging
 */
void reset() {
  timestamp_pos = 0;
  timestamp = 0;
}

/* 
  Send timestamp array over to the imp 
  */
void sendImpValues() {
  impSerial.print("$"); // start delimiter 
  impSerial.print("{\"id\": ");
  for (int i = 0; i < UUID_SIZE;i++) {
    impSerial.print(UUID[i], DEC);
  }
  impSerial.print(", \"values\":[");
  for (int i = 0; i< timestamp_pos; i++) {
    impSerial.print(timestamps[i]);
    if (i < timestamp_pos - 1) {
      impSerial.print(", ");
    }
  }
  impSerial.print("]}");
  
  impSerial.println("#"); // end delimiter for imp code 
//  Serial.print("UUID:");
//  for (int i = 0; i< 16; i++) {
//    Serial.println(UUID[i], DEC);
//  }
  Serial.println("Sent handshake data to the imp");
  sendMessage = true;
  led_count = 0;
  reset();
}

void loop(void)
{
  // check if the device got synced
  if (!synced) {
    int ref = 0;
    if ( checkImpSerial(ref) ){
      Serial.print("available: ");
      Serial.println(ref);
      if (ref == 1) {
        synced = true;
        ledError = false;
        Serial.print("sync is true");
        // Store the sync flag so we know we're synced
        EEPROM_writeAnything(START_UP_SYNC_ADDRESS, START_UP_SYNC_CODE);
      } else {
        ledError = true;
      }
    }
  }
  
  // check for any incoming packets
  checkReceive();
  
  // get current value
  current = analogRead(ANALOG_PIN);
//  Serial.println(current);

   // check if value has crossed over median
  if (crossedAvg()) {
//    Serial.print("prev: ");
//    Serial.print(prev);
//    Serial.print(" current: ");
//    Serial.println(current);
    if (timestamp_pos >= MAX_CROSSINGS) {
      //send over the data
      sendImpValues();
    } else if (timestamp_pos == 0) {
      timestamp = 0;
      timestamps[timestamp_pos] = 0;
      timestamp_pos++;
    } else if (timestamp_pos < MAX_CROSSINGS && 
      timestamp < TIME_THRESHOLD_MAX && timestamp > TIME_THRESHOLD_MIN) {
      timestamps[timestamp_pos] = timestamp;
      timestamp = 0;
      timestamp_pos++;
    }
  } else if (timestamp_pos >= MIN_CROSSINGS && timestamp > TIME_THRESHOLD_MAX){
    // send over the data
    sendImpValues();
  } else if (timestamp_pos < MIN_CROSSINGS && timestamp > TIME_THRESHOLD_MAX) {
    reset();
  }
  
  if ( sendMessage ) {
    sendMessage = false;
    Serial.print("Now broadcasting UUID ");
    for (int i = 0; i < UUID_SIZE;i++) {
      Serial.print(UUID[i], DEC);
    }
    radio.stopListening();
    radio.write( &UUID, UUID_SIZE );
    radio.startListening();
    Serial.println("... Done");
  }

  flash_led();
  
  if (timestamp_pos != 0) {
    timestamp++;
  }
  prev = current;
  
  delay(DELAY_TIME);
}

// checking imp values
boolean checkImpSerial(int & ref) {
  if (impSerial.available()) {
    int temp = impSerial.read();
    ref = temp;
//    Serial.print("available ");
//    Serial.println(ref);
    return true;
  }
  return false;
}


void clearSync() {
  manuallyClearEEPROM(START_UP_SYNC_ADDRESS, sizeof(START_UP_SYNC_CODE));
}


/*****************************************

Device UUID Code

*****************************************/


/* 
  Returns a boolean whether not not
  the item is stored by testing 
  for the existence of the start up 
  string code.
  */
boolean isStoredEEPROM(const char * checkString, const int address) {
  
 int codeSize = (sizeof(checkString));
 
 for (int i = 0; i < codeSize-1; i++) {
     if (checkString[i] != EEPROM.read(address + i)) {
       return false;
     }
   } 
   
   return true;
}

/* 
  Pulls the UUID out of PROM and into
  the global UUID variable.
  */
void loadDeviceUUIDFromEEPROM() {
  // Grab all the bytes from the UID in EEPROM
  for (int i = 0; i < UUID_SIZE; i++) {
     UUID[i] = EEPROM.read(START_UUID_ADDRESS + i);
  } 
}

/* 
  Creates a new UUID, stores it in EEPROM,
  and loads it into the global UUID variable.
  */
void createNewUUID() {
  // Clear some space for the uuid
  uint8_t *address = (uint8_t *)malloc(UUID_SIZE);;
  memset(address, '\0', UUID_SIZE);
  
  // Create the uuid
  TrueRandom.uuid(address); 
  
  Serial.println("UUID created: ");
  
  // Store that UUID in EEPROM
  for (int i = 0; i < UUID_SIZE; i++) {
    EEPROM.write(START_UUID_ADDRESS + i, *(address + i));
    UUID[i] = *(address + i);
    Serial.print(UUID[i]);
  }
  
  Serial.println("");
  
  // Free the space at the address
  free(address);
  
  // Store the start up code so we know we have a uuid
  EEPROM_writeAnything(START_UP_STRING_CODE_ADDRESS, START_UP_STRING_CODE);
}


/* 
  Helper method for clearing EEPROM.
  */
void manuallyClearEEPROM(int address, int s) {
  for (int i = 0; i < s; i++) {
    EEPROM.write(address + i, 0);
  }
}

/* 
  Useful debugging method for clearing the UUID.
  */
void clearUUID() {
  manuallyClearEEPROM(START_UUID_ADDRESS, UUID_SIZE);
  manuallyClearEEPROM(START_UP_STRING_CODE_ADDRESS, sizeof(START_UP_STRING_CODE));
}

/* 
  Print the UUID out in human readable form
  */
void printUUID() {
  if (isStoredEEPROM(START_UP_STRING_CODE, START_UP_STRING_CODE_ADDRESS)) {
    
      for (int i = 0; i < UUID_SIZE; i++) {
      Serial.print(UUID[i], DEC);
    }
   Serial.println("");  
  }  
  else {
     Serial.println("No UUID Stored"); 
  }
}
