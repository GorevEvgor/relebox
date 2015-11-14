#include <AltSoftSerial.h>
#include <Time.h>
#include <OneWire.h>

#define VENT_pin 4
#define HEAT_pin 5
#define SENS_pin 6
#define CD_pin 10 //Bluetooth CD pin
#define LED_pin 13

#define revent 6
#define goal 2.0
#define delta 3.0

typedef struct VH_t
  {
//    char *name;
    byte vent;
    byte heat;
    int wait;
    int count;
  };

typedef struct sens_t
  {
    byte addr[8];
    float temp;
  };

OneWire  ds(SENS_pin);  // 4.7K resistor is necessary
AltSoftSerial altSerial; // pin 8 9

VH_t VH_param[4] =
{
  {  //0 start vent
//    "vent1",   // name
    HIGH,      // vent
    LOW,      // heat
    60 * 2,  // wait
    0        //count
  },
  {  //1 wait
//    "wait", 
    LOW, LOW, 60 * 10, 0
  },
  {  //2 heat
//    "heat", 
    HIGH, HIGH, 60 * 4, 0
  },
  {  //3 post vent
//    "vent2", 
    HIGH, LOW, 60 * 8, 0
  },
};

sens_t sensor[2] = 
{
  {  // DOWN
    { 0x28, 0xBA, 0x58, 0xA9, 0x6, 0x0, 0x0, 0x96 }, // addr 
    0.0  //temp
  },
  {  //UP
    { 0x28, 0x5C, 0x3B, 0xAB, 0x6, 0x0, 0x0, 0x55 },
    0.0
  }
};

byte data[12];
time_t start;
byte state;
float startTemp;
byte wait_count;
byte error = 0;
byte debug = 0;

void setup(void) {

  int monitor_time = 60;

  set_time();

  Serial.begin(9600);
  altSerial.begin(9600);

  pinMode(VENT_pin, OUTPUT);
  pinMode(HEAT_pin, OUTPUT);
  pinMode(LED_pin, OUTPUT);
  pinMode(CD_pin, INPUT);

  get_temp();
  setState(0);
  
  while (now() < start + monitor_time and !Serial.available() ) 
    delay(500);
  
  while ( Serial.available() ) {
    char c = Serial.read();
    debug = 1;
  }

}

void setState(byte newState) {
  digitalWrite(VENT_pin,VH_param[newState].vent);
  digitalWrite(HEAT_pin,VH_param[newState].heat);
  state = newState;
  start = now();
  startTemp = sensor[0].temp;
  VH_param[newState].count++;
}


void loop(void) {
  digitalWrite(LED_pin,(error)? HIGH: LOW);
  switch (state) {
    case 0: //vent start
      wait_count = 0;
      if (now() > start + VH_param[state].wait) {
        get_temp();
        setState((sensor[0].temp > goal or (error & 0x3 == 0x3) )? 1: 2);
      }
      break;
     case 1: //wait
      if (now() > start + VH_param[state].wait) {
        get_temp();
        setState((( sensor[0].temp < goal + delta ) or ( wait_count++ > revent ))? 0:1 ); 
      }
      break;
     case 2: //heat
      if (now() > start + VH_param[state].wait) setState(3);
      break;
     case 3: //after heat
      if (now() > start + VH_param[state].wait) setState(1);
      break;
  }
  
  print_data();
    
  delay(10000);
}

void print_data() {
  byte i;
  byte cd = 1;
  for ( i = 0; i < 4; i++) {
    cd &= digitalRead(CD_pin);
    delay(100);
  }
      
  if (!cd and !debug) return;
  
  get_temp();
  if (debug) {
    Serial.print(minute());
    Serial.print(":");
    Serial.print(second());
    Serial.print(" DN=");  Serial.print(sensor[0].temp);
    Serial.print(" UP=");  Serial.print(sensor[1].temp);
    Serial.print(" State="); Serial.print(state);
    Serial.print(" count="); Serial.print(wait_count);
    Serial.print(" Time=");  Serial.print(now() - start);
    Serial.print(" is ");  Serial.print(VH_param[state].wait);
    Serial.print(" Error=");  Serial.print(error);
    Serial.print(" CD=");  Serial.print(cd);
    Serial.println();
  }
  if (cd) {
    altSerial.print(minute());
    altSerial.print(":");
    altSerial.print(second());
    altSerial.print(" ");
    altSerial.print("D="); altSerial.print(sensor[0].temp);
    altSerial.print(" U="); altSerial.print(sensor[0].temp);
    altSerial.print(" St="); altSerial.print(state);
    altSerial.print(" W="); altSerial.print(wait_count);
    altSerial.print(" T="); altSerial.print(now() - start);
    altSerial.print(" is "); altSerial.print(VH_param[state].wait);
    altSerial.print(" E="); altSerial.print(error);
    altSerial.print(" H="); altSerial.print(VH_param[2].count);
    altSerial.println();
  }
/*
  Serial.print(hour());
  Serial.print(" ");
  Serial.print(minute());
  Serial.print(" ");
  Serial.print(second());
  Serial.print(" ");
  Serial.print(year());
  Serial.print(" ");
  Serial.print(month());
  Serial.print(" ");
  Serial.print(day());
  Serial.print(" ");
  Serial.print(now());
  Serial.println();
*/

}

void set_time() {
  const unsigned long DEFAULT_TIME = 1357041600; // Jan 1 2013
  setTime(DEFAULT_TIME);
}

void get_temp(void) {
  byte i,sen;
  byte present = 0;
  byte pe;
  
  for (sen = 0; sen < 2; sen++) {
    
    ds.reset();
    ds.select(sensor[sen].addr);
    ds.write(0x44, 1);        // start conversion, with parasite power on at the end
  
    delay(1000);     // maybe 750ms is enough, maybe not
    // we might do a ds.depower() here, but the reset will take care of it.
  
    present = ds.reset();
    ds.select(sensor[sen].addr);
    ds.write(0xBE);         // Read Scratchpad

    pe = 0;
    for ( i = 0; i < 9; i++) {           // we need 9 bytes
      data[i] = ds.read();
      pe |= data[i];
    }
    int16_t raw = (data[1] << 8) | data[0];
    byte cfg = (data[4] & 0x60);
    // at lower res, the low bits are undefined, so let's zero them
    if (cfg == 0x00) raw = raw & ~7;  // 9 bit resolution, 93.75 ms
    else if (cfg == 0x20) raw = raw & ~3; // 10 bit res, 187.5 ms
    else if (cfg == 0x40) raw = raw & ~1; // 11 bit res, 375 ms
    //// default is 12 bit resolution, 750 ms conversion time
    sensor[sen].temp = (float)raw / 16.0;
    if (!pe)  {
      error |= 1 << sen;  //data not read
    } else {
      error &= !(1 << sen);  //clear error
    };
  }
  if ( (error & 0x3) == 0x1 ) sensor[0].temp=sensor[1].temp; // if sensor[0] not work use sensor[1]
}

