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
const int TIME_THRESHOLD_MIN = 20;
const int TIME_THRESHOLD_MAX = 400;
const int MIN_CROSSINGS = 4;
const int AVG_SIZE = 5; // average the first 5 values for a baseline
const int DELAY_TIME = 3; 
const int THRESHOLD = 2; // accelerometer values must be greater than this
const short ANALOG_PIN = 5; 
const short LED_PIN = 2;

long timestamp = 0;
long timestamps[MIN_CROSSINGS];
int timestamp_pos = 0;
int prev = 0;
int current = 0;
int avg = 0;

// nRF24L01 setup on SPI bus plus pins 6 & 10
RF24 radio(6,10);
const short ROLE_PIN = 7;
const long MIN_WAIT = 30;
const long MAX_WAIT = 500;
char * UUID = "MNB890";
char * receivedUUID = "000000"; // blank UUID for the received message

long counter = 0;
long counter_wait = 0;
boolean sendMessage = false;

// Single radio pipe address for all the nodes to communicate.
const uint64_t pipe = 0xF0F0F0F0E1LL;

void setCounter(){
  counter_wait = random(MIN_WAIT, MAX_WAIT);
  Serial.println(counter_wait);
}

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

  // Dump the configuration of the rf unit for debugging
  radio.printDetails();
  
  // set the counter wait time
  setCounter();
  
}

boolean checkReceive() {
   // if there is data
  if ( radio.available() )
  {
    bool finished = false;
    while (!finished)
    {
      finished = radio.read( &receivedUUID, sizeof(receivedUUID) );
      Serial.print("Got payload ");
      Serial.println(receivedUUID);
    }
  }
}

boolean shouldSend() {
  if (counter > counter_wait && sendMessage) {
    Serial.print("send now ");
    Serial.println(counter);
    counter = 0;
    sendMessage = false;
    setCounter();
    return true;
  }
  return false;
}

boolean crossedAvg() {
  return ((prev < avg && current > avg) || (prev > avg && current < avg));
}

void flash_led() {
  digitalWrite(LED_PIN, HIGH);
  delay(1000);
  digitalWrite(LED_PIN, LOW);
}

void reset() {
//  Serial.println("reset");
  timestamp_pos = 0;
  timestamp = 0;
}

void loop(void)
{

  // check for any incoming packets
  checkReceive();
  
  // get current value
  current = analogRead(ANALOG_PIN);
  
   // check if value has crossed over median
  if (crossedAvg()) {
    
    if (timestamp_pos == 0) {
      timestamp = 0;
      timestamps[timestamp_pos] = 0;
      timestamp_pos++;
    } else if (timestamp_pos < MIN_CROSSINGS) {
      if (timestamp < TIME_THRESHOLD_MIN && timestamp_pos != 0) {
        reset();
      }
      if (timestamp < TIME_THRESHOLD_MAX && timestamp > TIME_THRESHOLD_MIN) {
        timestamps[timestamp_pos] = timestamp;
        timestamp = 0;
        timestamp_pos++;
      }
      if (timestamp_pos == MIN_CROSSINGS) {

        impSerial.print("{\"id\": ");
        impSerial.print(UUID);
        impSerial.print(", \"values\":[");
        for (int i = 0; i< MIN_CROSSINGS; i++) {
          impSerial.print(timestamps[i]);
          if (i < MIN_CROSSINGS - 1) {
            impSerial.print(", ");
          }
        }
        impSerial.println("]}");
        Serial.println("Handshake");
        sendMessage = true;
        flash_led();
        reset();
      }
    } else {
      reset();
    }
  }
  
  if ( shouldSend() ) {
    radio.stopListening();
    Serial.print("Now sending ");
    Serial.println(UUID);
    radio.write( &UUID, sizeof(UUID) );
    radio.startListening();
  }
  
  if (sendMessage) {
//    Serial.println(counter);
    ++counter;
  }
  
  // if its gone over the max time threshold reset everything
  if (timestamp > TIME_THRESHOLD_MAX) {
    reset();
  }
  if (timestamp_pos != 0) {
    timestamp++; // make sure this isn't overflowing
  }
  prev = current;
  
  delay(DELAY_TIME);
}
