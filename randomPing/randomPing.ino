/*
 * Handshake & random ping
 */

/**
 * proximity sensing by having random delay in sending
 */

#include <SPI.h>
#include "nRF24L01.h"
#include "RF24.h"
#include "printf.h"
#include <SoftwareSerial.h>

// set up the imp
SoftwareSerial impSerial(8, 9);

// handshake and accelerometer thresholds
const int TIME_THRESHOLD_MIN = 100;
const int TIME_THRESHOLD_MAX = 500;
const int MIN_CROSSINGS = 4;
const int MAX_CROSSINGS = 40;
const int THRESHOLD = 5;
const int MAX_THRESHOLD = 10;
const int AVG_SIZE = 10; // average the first values for a baseline
const int DELAY_TIME = 1; 
const short ANALOG_PIN = 5; 
const short LED_PIN = 5;
const int LED_DELAY = 1000;

int led_count = -1;
long timestamp = 0;
long timestamps[MAX_CROSSINGS];
int timestamp_pos = 0;
int prev = 0;
int current = 0;
int avg = 0;

// nRF24L01 setup on SPI bus plus pins 6 & 10
RF24 radio(6,10);
const short ROLE_PIN = 7;
char * UUID = "MNB890";
char * receivedUUID = "000000"; // blank UUID for the received message

boolean sendMessage = false;

// Single radio pipe address for all the nodes to communicate.
const uint64_t pipe = 0xF0F0F0F0E1LL;

void setup(void)
{
  // set up the role pin
  pinMode(ROLE_PIN, INPUT);
  digitalWrite(ROLE_PIN,HIGH);
  delay(20); // Just to get a solid reading on the role pin

  // read the address pin, establish our role
  if ( ! digitalRead(ROLE_PIN) )
    UUID = "ABC123";
  
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
}

boolean checkReceive() {
   // if there is data
  if ( radio.available() )
  {
    bool finished = false;
    while (!finished)
    {
      finished = radio.read( &receivedUUID, sizeof(receivedUUID) );
      Serial.print("Sending received UUID to the imp: ");
      Serial.println(receivedUUID);
      impSerial.print("$");
      impSerial.print("{\"id\": ");
      impSerial.print(UUID);
      impSerial.print(", \"received\": \"");
      impSerial.print(receivedUUID);
      impSerial.print("\"}");
      impSerial.println("#");
    }
  }
}

boolean crossedAvg() {
  return (((prev < avg && current > avg) || (prev > avg && current < avg)) && 
    abs(current - prev) >= THRESHOLD && abs(current-prev) < MAX_THRESHOLD );
}

void flash_led() {
  if (led_count != -1 && led_count <= LED_DELAY) {
    ++led_count;
    digitalWrite(LED_PIN, HIGH);
  } else if (led_count == -1 || led_count > LED_DELAY) {
    led_count = -1;
    digitalWrite(LED_PIN, LOW);
  }
}

void reset() {
  timestamp_pos = 0;
  timestamp = 0;
}

//impSerial
void sendImpValues() {
  Serial.print("$"); // start delimiter 
  Serial.print("{\"id\": ");
  Serial.print(UUID);
  Serial.print(", \"values\":[");
  for (int i = 0; i< timestamp_pos; i++) {
    Serial.print(timestamps[i]);
    if (i < timestamp_pos - 1) {
      Serial.print(", ");
    }
  }
  Serial.print("]}");
  Serial.println("#"); // end delimiter for imp code 
  Serial.print("UUID:");
  Serial.println(UUID);
  Serial.println("Sent handshake data to the imp");
  sendMessage = true;
  led_count = 1;
  reset();
}

void loop(void)
{

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
    Serial.println(UUID);
    radio.stopListening();
    radio.write( &UUID, sizeof(UUID) );
    radio.startListening();
  }

  flash_led();
  
  if (timestamp_pos != 0) {
    timestamp++; // make sure this isn't overflowing
  }
  prev = current;
  
  delay(DELAY_TIME);
}
