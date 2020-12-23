#include <SPI.h>
#include <SD.h>
#include <RH_RF95.h>
#include <epd2in9.h>
#include <epdpaint.h>
#include <Wire.h>
#include "RTClib.h"
 
#define RFM95_CS 8
#define RFM95_RST 4
#define RFM95_INT 3
#define COLORED     0
#define UNCOLORED   1

// RTC Data
RTC_PCF8523 rtc;
int rtc_day=0, rtc_month=0, rtc_year=0, rtc_hour=0, rtc_min=0, rtc_sec=0;
DateTime now;

// SD Data
#define SD_CS 5
File dataFile;
 
// RFM Data
#define RF95_FREQ 434.0
RH_RF95 rf95(RFM95_CS, RFM95_INT);
int last_rssi = 0;
unsigned char radiopacket[50];
unsigned char my_id = 0x10;

// E-Paper Data
unsigned char image[1024];
Paint paint(image, 0, 0);    
Epd epd;
int display_act_pos = 0;

// Monitor
char rec_buf[1024], rec_byte;
char debug_val = 0;

// Allgemein
#define LED 13
int value = 0;
double spannung = 0.0;

char global_buf[1024];
char voltage_buf[64];
char voltage_buf1[64] = "--.-- V";
char voltage_buf2[64] = "--.-- V";
char *p;
char flagTriggerCollectMode = 0;
char flagDispRefresh = 0;

void setup() 
{
  Serial.begin(9600);
  delay(2000);
  Serial.println("Programmstart...");  

  // RTC
  if (! rtc.begin()) {
    Serial.println("Couldn't find RTC");
    while (1);
  }
  else
  {
    Serial.println("RTC found");
  }
  
  if (! rtc.initialized()) {
    Serial.println("RTC is NOT running!");
  }
  else
  {
    Serial.println("RTC Init OK");
  }
  
  // RFM
  pinMode(LED, OUTPUT);     
  pinMode(RFM95_RST, OUTPUT);
  digitalWrite(RFM95_RST, HIGH);  
   
  // Manual reset
  digitalWrite(RFM95_RST, LOW);
  delay(10);
  digitalWrite(RFM95_RST, HIGH);
  delay(10);
 
  if (!rf95.init()) {
    Serial.println("LoRa radio init failed");
    while (1);
  }
  else
  {
    Serial.println("LoRa Init OK");
  }  
  
  if (!rf95.setFrequency(RF95_FREQ)) {
    Serial.println("setFrequency failed");
    while (1);
  }
  Serial.print("LoRa Freq: "); Serial.println(RF95_FREQ);
 
  rf95.setTxPower(23, false);


  // E-Paper
  if (epd.Init(lut_full_update) != 0) {
      Serial.print("Display Init 1 failed");
      while (1);
  }
  else
  {
    Serial.println("Display Init 1 OK");
  }
    
  epd.ClearFrameMemory(0xFF);   // bit set = white, bit reset = black
  epd.DisplayFrame();
  epd.ClearFrameMemory(0xFF);   // bit set = white, bit reset = black
  epd.DisplayFrame();

  if (epd.Init(lut_partial_update) != 0) 
  {
      Serial.print("Display Init 2 failed");
  }  
  else
  {
    Serial.println("Display Init 2 OK");
  }

  paint.SetWidth(128);
  paint.SetHeight(24);
  paint.SetRotate(ROTATE_0);

  // SD
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS,HIGH);

  // see if the card is present and can be initialized:
  if (!SD.begin(SD_CS)) {
    Serial.println("Card failed, or not present");
    while (1);
  }
  else
  {
    Serial.println("SD card Init OK");
  }  

  Serial.println("--------------------------------------------------------------------------------------------------------------");
  Serial.println("Kommandos:");
  Serial.println("");
  Serial.println("rtc_set [day].[month].[year] [hour]:[min]:[sec]         -            Setzt das angegebene Datum               ");
  Serial.println("rtc_get                                                 -            Holt das Datum / Uhrzeit                 ");
  Serial.println("debug_on                                                -            Schaltet die Debugausgabe ein            ");
  Serial.println("debug_off                                               -            Schaltet die Debugausgabe aus            ");
  Serial.println("del_log1                                                -            Löscht die aktuelle Logdatei1 (Neubeginn)");
  Serial.println("del_log2                                                -            Löscht die aktuelle Logdatei2 (Neubeginn)");
  Serial.println("collect_data                                            -            Daten aufzeichnen                        ");
  Serial.println("disp_refresh                                            -            Display auffrischen                      ");
  Serial.println("");
  Serial.println("--------------------------------------------------------------------------------------------------------------");
  Serial.println("");
  Serial.print(">");

  get_rtc_data();
  setDispData();
}

void loop()
{
  get_rtc_data();
  
  if (rf95.available())
  {
    digitalWrite(LED, HIGH);

    uint8_t buf[RH_RF95_MAX_MESSAGE_LEN];
    uint8_t len = sizeof(buf);
    
    if (rf95.recv(buf, &len))
    {
      if((buf[0] == my_id) && ((buf[1] == 0x20) || (buf[1] == 0x21)))
      {
        // Trigger Collect Mode
        if(flagTriggerCollectMode == 1)
        {
          if(debug_val) Serial.println("Trigger Collect Mode");
          
          radiopacket[0] = buf[1];
          radiopacket[1] = my_id;
          radiopacket[2] = 0xA1;      
      
          if(debug_val) sprintf(global_buf, "Sende: 0x%02x 0x%02x 0x%02x", radiopacket[0], radiopacket[1], radiopacket[2]); Serial.println(global_buf);
          rf95.send((uint8_t *)radiopacket, 3);
          flagTriggerCollectMode = 0;
        }
                               
        if(buf[5] == 0x01)
        {
          Serial.println("Collect started...");
        }
        else if(buf[5] == 0xFF)
        {
          Serial.println("Finished");
          Serial.print(">");
        }
        else if(buf[5] == 0xAA)
        {         
          value = (buf[4] << 8) + (buf[3] << 4) + (buf[2]);
          spannung = (((double)value) / 100.0);

          sprintf(voltage_buf, "%s", String(spannung).c_str());
          replacechar(voltage_buf, '.', ',');
          sprintf(global_buf, "0x%02x: %sV", buf[1], voltage_buf);         
          Serial.println(global_buf);
        }
        else
        { 
          last_rssi = rf95.lastRssi();       
          if(debug_val){sprintf(global_buf, "RSSI: %i", last_rssi); Serial.println(global_buf);} 
          if(debug_val){sprintf(global_buf, "Empfange: %02x %02x %02x %02x", buf[0], buf[1], buf[2], buf[3]); Serial.println(global_buf);}
          
          value = (buf[4] << 8) + (buf[3] << 4) + (buf[2]);
          sprintf(voltage_buf, "%01i%01i,%01i%01i", value / 1000, value % 1000 / 100, value % 100 / 10, value % 10);
          if(voltage_buf1 == "") sprintf(voltage_buf1, "--,-- V");
          if(voltage_buf2 == "") sprintf(voltage_buf2, "--,-- V");
          
          // Umwandlung und SD Schreiben
    		  if(buf[1] == 0x20 && value > 500)
    		  {
            sprintf(voltage_buf1, "%s", voltage_buf);  
            if(debug_val) Serial.println(voltage_buf1);
            if(debug_val) Serial.println("Open file 1");
    		    dataFile = SD.open("datalog1.csv", FILE_WRITE);
    		  }
    		  else if(buf[1] == 0x21 && value > 500)
    		  {
            sprintf(voltage_buf2, "%s", voltage_buf);   
            if(debug_val) Serial.println(voltage_buf2);
            if(debug_val) Serial.println("Open file 2");
    		    File dataFile = SD.open("datalog2.csv", FILE_WRITE);
    		  } 
          
          sprintf(global_buf, "%02i.%02i.%04i %02i:%02i:%02i;%s;0", now.day(), now.month(), now.year(), now.hour(), now.minute(), now.second(), voltage_buf);  
  
          if(dataFile)
          {
            dataFile.println(global_buf); 
            dataFile.close();
            if(debug_val) Serial.println("SD Write successful");
          }
          else 
          {
            Serial.println("Error opening File");
          }

          setDispData();
        }
      }
    }
    else
    {
      Serial.println("Receive failed");
    }

    memset(global_buf, 0, sizeof(global_buf));
    digitalWrite(LED, LOW);
  }

  // Monitor

  if(Serial.available())
  {
    rec_byte = Serial.read();
    Serial.print(rec_byte);
    sprintf(rec_buf, "%s%c", rec_buf, rec_byte);

    if(strstr(rec_buf, "\r"))
    {     
      Serial.println("");
      
      p=strstr(rec_buf, "rtc_set");
      if(p)
      {
        p += strlen("rtc_set") + 1;
        sscanf(p, "%02i.%02i.%04i %02i:%02i:%02i", &rtc_day, &rtc_month, &rtc_year, &rtc_hour, &rtc_min, &rtc_sec);
        rtc.adjust(DateTime(rtc_year, rtc_month, rtc_day, rtc_hour, rtc_min, rtc_sec));
        Serial.println("New RTC value set");
      }

      p=strstr(rec_buf, "rtc_get");
      if(p)
      {
        sprintf(global_buf, "RTC value: %02i.%02i.%04i %02i:%02i:%02i", rtc_day, rtc_month, rtc_year, rtc_hour, rtc_min, rtc_sec);
        Serial.println(global_buf);
      }

      p=strstr(rec_buf, "debug_on");
      if(p)
      {
        debug_val = 1;
        Serial.println("Debug Mode ON");
      }

      p=strstr(rec_buf, "debug_off");
      if(p)
      {
        debug_val = 0;
        Serial.println("Debug Mode OFF");
      }

      p=strstr(rec_buf, "collect_data");
      if(p)
      {
        flagTriggerCollectMode = 1;
        Serial.println("Collect Mode ON... Waiting");
      }

      p=strstr(rec_buf, "del_log1");
      if(p)
      {
        if(SD.remove("datalog1.csv")) Serial.println("Logfile 1 deleted");
        else Serial.println("Delete Logfile 1 failed");
      }

      p=strstr(rec_buf, "del_log2");
      if(p)
      {
        if(SD.remove("datalog2.csv")) Serial.println("Logfile 2 deleted");
        else Serial.println("Delete Logfile 2 failed");
      }

      p=strstr(rec_buf, "disp_refresh");
      if(p)
      {
        flagDispRefresh = 1;
      }

      p=0;
      Serial.print(">");
      memset(rec_buf, 0, sizeof(rec_buf));
    }

    rec_byte = 0;    
  }  
}

int replacechar(char *str, char orig, char rep) {
    char *ix = str;
    int n = 0;
    while((ix = strchr(ix, orig)) != NULL) {
        *ix++ = rep;
        n++;
    }
    return n;
}

void setDispData(void)
{
  sprintf(global_buf, "%02i.%02i.%04i", rtc_day, rtc_month, rtc_year);
  addDisplay(global_buf, &Font16, 15);
  
  sprintf(global_buf, "%02i:%02i:%02i", rtc_hour, rtc_min, rtc_sec);
  addDisplay(global_buf, &Font16, 50);

  sprintf(global_buf, "Nissan:");
  addDisplay(global_buf, &Font20, 25);
  
  sprintf(global_buf, "%s V", voltage_buf1);
  addDisplay(global_buf, &Font24, 50);

  /*
  sprintf(global_buf, "0x21:");
  addDisplay(global_buf, &Font20, 25);
  
  sprintf(global_buf, "%s V", voltage_buf2);
  addDisplay(global_buf, &Font24, 30);
  */
  
  showDisplay();
}

void addDisplay(char *str, sFONT* font, int space)
{
  paint.Clear(UNCOLORED);
  paint.DrawStringAt(0, 0, str, font, COLORED);
  epd.SetFrameMemory(paint.GetImage(), 0, display_act_pos, paint.GetWidth(), paint.GetHeight());

  display_act_pos += space;  
}

void showDisplay(void)
{
  epd.DisplayFrame();  
  display_act_pos = 0;
}

void get_rtc_data(void)
{
  now = rtc.now();
  rtc_day = now.day();
  rtc_month = now.month();
  rtc_year = now.year();
  rtc_hour = now.hour();
  rtc_min = now.minute();
  rtc_sec = now.second();
}

