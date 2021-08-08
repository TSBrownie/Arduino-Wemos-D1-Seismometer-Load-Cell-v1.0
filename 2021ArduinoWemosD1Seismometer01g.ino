//Arduino, WeMos D1R1 (ESP8266 Boards 2.5.2), upload 921600, 80MHz, COM:7,WeMos D1R1 Boards 2
//Program: Creates SD card file, opens, writes, prints out.  Tested with 64GB SD.
//Takes reading when values above / below set limits.
//WeMos Micro SD Shield uses HSPI(12-15) not (5-8), 3V3, G:
//GPIO12(D6)=MISO (main in, secondary out); GPIO13(D7)=MOSI (main out, secondary in) 
//GPIO14(D5)=CLK (Clock); GPIO15(D8)=CS (Chip select)
//SD library-->8.3 filenames (ABCDEFGH.txt = abcdefgh.txt)
//RTC DS1307. I2C--> SCL(clk)=D1, SDA(data)=D2 
//XFW-HX711 Load Cell A2D.  +3,+5=>VCC; Gnd=>Gnd; DT=>GPIO0=D4 or GPIO16=D0; SCK=>GPIO2=D3.
//Load Cell: Red = E+, Blk = E-, Wht = A-, Grn = A+  
//20210806 - TSBrownie.  Non-commercial use.
#include <SD.h>               //SD card library
#include <SPI.h>              //Serial Peripheral Interface bus lib for COMM, SD com
#include "Wire.h"             //I2C library
#include "HX711.h"            //HX711 Load Cell library
HX711 scale;                  //Link to HX711 lib function
#define DS1307 0x68           //I2C Addr of RTC1307 (Default=0x68 Hex)
File diskFile;                //Link to file name
String SDData;                //Build data to write to SD "disk"
String timeString;            //Build date time data
bool OneTimeFlag = true;      //For demo, execute 1 time (remove for logging)
String DoWList[]={"Null",",Sun,",",Mon,",",Tue,",",Wed,",",Thr,",",Fri,",",Sat,"}; //DOW from 1-7
byte second, minute, hour, DoW, Date, month, year;     //Btye variables for BCD time
String FName = "SDfil01.txt"; //SD card file name to create/write/read
unsigned int interval = 1000; //Time in milliseconds between pin readings 
const int LOADCELL_DOUT = 0;  //GPIO0 = D3  or  GPIO16 = D0  yellow wire 
const int LOADCELL_SCK = 2;   //GPIO2 = D4 brn wire
long calib = 0L;              //Scale offset to zero
long reading = 0L;            //Load cell reading
long value = 0L;              //Temp value from readScale
long minValue = -1000L;       //Noise Filter. Below keep data. Zero for continuous.
long maxValue = 1000L;        //Noise Filter. Above keep data. Zero for continuous.
int j = 0;                    //Testing. Show stored every j times, then delete file.
unsigned long calibTimer = 600000L;   //Time recalibration. 60k=1 min, 3600k=1hr
unsigned long calibTimeLast = 0L;    //Last time of recalibration

//RTC FUNCTIONS =====================================
byte BCD2DEC(byte val){             //Ex: 51 = 01010001 BCD. 01010001/16-->0101=5 then x10-->50.  
  return(((val/16)*10)+(val%16));}  //         01010001%16-->0001. 50+0001 = 51 DEC

void GetRTCTime(){                               //Routine read real time clock, format data
  byte second;byte minute;byte hour;byte DoW;byte Date;byte month;byte year;
  Wire.beginTransmission(DS1307);                //Open I2C to RTC DS1307
  Wire.write(0x00);                              //Write reg pointer to 0x00 Hex
  Wire.endTransmission();                        //End xmit to I2C.  Send requested data.
  Wire.requestFrom(DS1307, 7);                   //Get 7 bytes from RTC buffer
  second = BCD2DEC(Wire.read() & 0x7f);          //Seconds.  Remove hi order bit
  minute = BCD2DEC(Wire.read());                 //Minutes
  hour = BCD2DEC(Wire.read() & 0x3f);            //Hour.  Remove 2 hi order bits
  DoW = BCD2DEC(Wire.read());                    //Day of week
  Date = BCD2DEC(Wire.read());                   //Date
  month = BCD2DEC(Wire.read());                  //Month
  year = BCD2DEC(Wire.read());                   //Year
  timeString = 2000+year;                        //Build Date-Time data to write to SD
  if (month<10){timeString = timeString + '0';}  //Pad leading 0 if needed
  timeString = timeString + month;               //Month (1-12)  
  if(Date<10){timeString = timeString + '0';}    //Pad leading 0 if needed
  timeString = timeString + Date;                //Date (1-30)
  timeString = timeString + DoWList[DoW];        //1Sun-7Sat (0=null)
  if (hour<10){timeString = timeString + '0';}   //Pad leading 0 if needed
  timeString = timeString + hour;                //HH (0-24)
  if (minute<10){timeString = timeString + '0';} //Pad leading 0 if needed
  timeString = timeString + minute;              //MM (0-60)
  if (second<10){timeString = timeString + '0';} //Pad leading 0 if needed
  timeString = timeString + second;              //SS (0-60)
}

//SD CARD FUNCTIONS =================================
void openSD() {                              //Routine to open SD card
  Serial.println(); Serial.println("Open SD card");    //User message
  if (!SD.begin(15)) {                       //If not open, print message.  (CS=pin15)
    Serial.println("Open SD card failed");
    return;}
  Serial.println("SD Card open");
}

char openFile(char RW) {                     //Open SD file.  Only 1 open at a time.
  diskFile.close();                          //Ensure file status, before re-opening
  diskFile = SD.open(FName, RW);}            //Open Read at end.  Open at EOF for write/append

String print2File(String tmp1) {             //Print data to SD file
  openFile(FILE_WRITE);                      //Open user SD file for write
  if (diskFile) {                            //If file there & opened --> write
    diskFile.println(tmp1);                  //Print string to file
    diskFile.close();                        //Close file, flush buffer (reliable but slower)
  } else {Serial.println("Error opening file for write");}   //File didn't open
}

void getRecordFile() {                       //Read from SD file
  if (diskFile) {                            //If file is there
    Serial.write(diskFile.read());           //Read, then write to COM
  } else {Serial.println("Error opening file for read");}    //File didn't open
}

//Load Cell FUNCTIONS =================================
long readScale(){
    if (scale.wait_ready_timeout(1000)) {     //Wait x millisec to get reading
      reading = scale.read();                 //Load cell reading
      return reading;                         //Return reading
  }else {Serial.println("HX711 Offline.");}   //Else print load cell offline
}

void recalibrate(){
  if (abs(calibTimeLast - millis()) > calibTimer){ //Recalibrate every x time
    calib = -readScale();                     //New calibration
    GetRTCTime();                             //Get time from real time clock
    SDData = timeString+','+'C'+','+calib;    //Prepare calibration string
    print2File(SDData);                       //Write string to SD file
    Serial.print("Re-Calibration: ");Serial.println(SDData);
    calibTimeLast = millis();                 //Keep current time for next interation
  }
}

// SETUP AND LOOP =============================
void setup() {                               //Required setup routine
  Wire.begin();                              //Join I2C bus as primary
  Serial.begin(74880);                       //Open serial com (74880, 38400, 115200)
  openSD();                                  //Call open SD card routine
  scale.begin(LOADCELL_DOUT, LOADCELL_SCK);  //Open load cell
  delay(1000);                               //Allow serial to startup
  Serial.println("2021ArduinoWemosD1Seismometer01g"); //Print program file name
  calib = -readScale();                      //Offset to zero load cell.
  Serial.print("Calibration: ");Serial.println(calib);
  delay(1000);                              //Allow SD to start
  GetRTCTime();                             //Get time from real time clock
  SDData = timeString+','+'C'+','+calib;    //Prepare calibration string
  print2File(SDData);                       //Write string to SD file
}

void loop() {                                //Get data, write timestamped records
    value = readScale() + calib;             //Calibrated data
    Serial.print("Value: "); Serial.println(value); //For continous data output
    if(value > maxValue or value < minValue){ //Decide if take reading
      Serial.print("Capturing Data ");Serial.println(timeString); //Inform user
      GetRTCTime();                          //Get time from real time clock
      SDData = timeString+','+'D'+','+value; //Prepare data string to write to SD
      print2File(SDData);                    //Write data to SD file
      j++;                                   //Testing only.
    } else {recalibrate();}                  //Time based Recalibration, if not busy
    if(j > 100){                             //Testing every 100 writes, dump SD file
      Serial.println("File Write Done");
      openFile(FILE_READ);                   //Testing. Open SD file at start for read
      while (diskFile.available()) {         //Testing. Read SD file until EOF
        getRecordFile();}                    //Testing. Get 1 line from SD file
      delay(4000);                           //Testing.Print out data wait x seconds
      j = 0;                                 //Testing only
    diskFile.close();                        //Testing. Close file, flush buffer.
    Serial.println("Printout File Done.  Delete File."); //Testing only. User info.
    SD.remove(FName);                        //Testing ONLY. Deletes file.    
    }
}
