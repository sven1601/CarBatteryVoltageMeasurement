#include <SPI.h>
#include <RH_RF95.h>
#include <Wire.h>
#include <Adafruit_ADS1015.h>
 
#define RFM95_CS 8
#define RFM95_RST 4
#define RFM95_INT 3
#define RF95_FREQ 434.0
#define LED 13

#define debug_mode 0
 
// Singleton instance of the radio driver
RH_RF95 rf95(RFM95_CS, RFM95_INT);
// Instance for the AD1015 
Adafruit_ADS1015 ads1015;

#define analogPin 3
#define donePin 5
#define overridePin 6
char flagCollectMode = 0;
int value = 0;
double spannung = 0.0;
char rec_flag = 0;
double adc_factor1 = 1.02;
double adc_factor2 = 1.0092;

unsigned char my_id = 0x20;
unsigned char target_id = 0x10;
unsigned char radiopacket[50];

void setup() 
{
  if(debug_mode) pinMode(LED, OUTPUT); 
  pinMode(donePin, OUTPUT);
  pinMode(overridePin, OUTPUT);
  digitalWrite(donePin, LOW);
  digitalWrite(overridePin, LOW);
  digitalWrite(LED, LOW);
  
  pinMode(RFM95_RST, OUTPUT);
  digitalWrite(RFM95_RST, HIGH);
 
  if(debug_mode) Serial.begin(9600);
  delay(100);
 
  // Reset
  digitalWrite(RFM95_RST, LOW);
  delay(10);
  digitalWrite(RFM95_RST, HIGH);
  delay(10);

  while (!rf95.init()) {
    if(debug_mode) Serial.println("LoRa radio init failed");
    while (1);
  }
  if(debug_mode) Serial.println("LoRa radio init OK!");

  if (!rf95.setFrequency(RF95_FREQ)) {
    if(debug_mode) Serial.println("setFrequency failed");
    while (1);
  }
  if(debug_mode){Serial.print("Set Freq to: "); Serial.println(RF95_FREQ);}
  
  rf95.setTxPower(23, false);  
  
  ads1015.begin();
  ads1015.setGain(GAIN_TWO);
}

void loop() 
{
  radiopacket[0] = target_id;
  radiopacket[1] = my_id;

  if(flagCollectMode == 0)
  {
    value = 0;
    for(int i=0; i<100; i++)
    {
      value += ads1015.readADC_SingleEnded(0);
    }
    value = (int)((double)(value / 100) * adc_factor1);    
    
    radiopacket[2] = value & 0x00F;
    radiopacket[3] = (value & 0x0F0) >> 4; 
    radiopacket[4] = (value & 0xF00) >> 8;  
    radiopacket[5] = 0x00;
    rf95.send((uint8_t *)radiopacket, 6);
    if(debug_mode){Serial.print("Spannung: "); Serial.println(value);}
    
    if(debug_mode) Serial.println("Kurz auf Nachricht warten...");
    for(int x=0;x<500000;x++)
    {
      if(rf95.available())
      {
        if(debug_mode) Serial.println(x);
        break;
      }
    }   

    if(rf95.available())
    {
      uint8_t buf[RH_RF95_MAX_MESSAGE_LEN];
      uint8_t len = sizeof(buf);  
      if(debug_mode) digitalWrite(LED, HIGH);
      
      if (rf95.recv(buf, &len))
      {
        if((buf[0] == my_id) && (buf[1] == 0x10))
        {
          if(buf[2] == 0xA1)        flagCollectMode = 1;
          if(debug_mode) Serial.print("Trigger Collect Mode");
        }
      }      
      if(debug_mode)
      {
        Serial.print("Empfangen: ");
        Serial.print(buf[0]);
        Serial.print(buf[1]);
        Serial.print(buf[2]);
      }
      delay(1000);
      if(debug_mode) digitalWrite(LED, LOW);
    }

    if(debug_mode) Serial.println("Warten ENDE");

    // Ende
    if(flagCollectMode == 0)
    {
      digitalWrite(overridePin, LOW);
      digitalWrite(donePin, HIGH);
      delay(5000);
      //return;
    }
  }

  else if(flagCollectMode == 1)
  {
    flagCollectMode = 0;
    digitalWrite(overridePin, HIGH);
    digitalWrite(donePin, LOW);
    delay(100);

    radiopacket[2] = 0;
    radiopacket[3] = 0; 
    radiopacket[4] = 0; 
    radiopacket[5] = 0x01;
    for(int i=0;i<3;i++) {rf95.send((uint8_t *)radiopacket, 6);}    

    for(int x=0;x<200;x++)
    {
      value = ads1015.readADC_SingleEnded(0);
      value = (int)((double)(value) * adc_factor2);
      radiopacket[2] = value & 0x00F;
      radiopacket[3] = (value & 0x0F0) >> 4; 
      radiopacket[4] = (value & 0xF00) >> 8; 
      radiopacket[5] = 0xAA;
      rf95.send((uint8_t *)radiopacket, 6);
      if(debug_mode) Serial.println(x);
      delay(100);
    }

    radiopacket[2] = 0;
    radiopacket[3] = 0; 
    radiopacket[4] = 0; 
    radiopacket[5] = 0xFF;
    for(int i=0;i<3;i++) {rf95.send((uint8_t *)radiopacket, 6);}

    digitalWrite(overridePin, LOW);
    digitalWrite(donePin, HIGH);
    delay(5000);
    //return;
  }

}
