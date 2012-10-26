/*
  Modified from example by J. Coliz
 */

/**
 * proximity sensing by having random delay in sending
 */

#include <SPI.h>
#include "nRF24L01.h"
#include "RF24.h"
#include "printf.h"

//
// Hardware configuration
//

// Set up nRF24L01 radio on SPI bus plus pins 9 & 10

RF24 radio(9,10);
const short role_pin = 7;
const long MIN_WAIT = 30000;
const long MAX_WAIT = 300000;
char * UUID = "MNB890";

char * receivedUUID = "000000";
long counter = 0;
long counter_wait = 0;
//
// Topology
//

// Single radio pipe address for the 2 nodes to communicate.
const uint64_t pipe = 0xF0F0F0F0E1LL;
  
//
// Role management
//
// Set up role.  This sketch uses the same software for all the nodes in this
// system.  Doing so greatly simplifies testing.  The hardware itself specifies
// which node it is.
//
// This is done through the role_pin
//

// The various roles supported by this sketch
typedef enum { role_sender = 1, role_receiver } role_e;

// The debug-friendly names of those roles
const char* role_friendly_name[] = { "invalid", "Sender", "Receiver"};

// The role of the current running sketch
role_e role;

void setCounter(){
  counter_wait = random(MIN_WAIT, MAX_WAIT);
  Serial.println(counter_wait);
}

void setup(void)
{
  //
  // Role
  //

  // set up the role pin
  pinMode(role_pin, INPUT);
  digitalWrite(role_pin,HIGH);
  delay(20); // Just to get a solid reading on the role pin

  // read the address pin, establish our role
  if ( ! digitalRead(role_pin) )
    UUID = "ABC123";
  
  Serial.begin(57600);
  printf_begin();
  Serial.println("Random Ping");

  //
  // Setup and configure rf radio
  //
  
  radio.begin();
  radio.setRetries(15,15);
  radio.setPayloadSize(8);
  radio.setPALevel(RF24_PA_MIN);
  radio.setDataRate(RF24_250KBPS);
  radio.enableAckPayload();

  // open up the reading pipe
  radio.openReadingPipe(1,pipe);

  radio.startListening();

  //
  // Dump the configuration of the rf unit for debugging
  //

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
  if (counter > counter_wait) {
    counter = 0;
    setCounter();
    return true;
  }
  return false;
}

void loop(void)
{

  // check receiving
  checkReceive();
  
  if ( shouldSend() ) {
    radio.stopListening();
    unsigned long time = millis();
    Serial.print("Now sending ");
    Serial.println(UUID);
    radio.write( &UUID, sizeof(UUID) );
    radio.startListening();
  }
  
  ++counter;
}
