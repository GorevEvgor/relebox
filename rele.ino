#include <AltSoftSerial.h>
#include <Time.h>
#include <OneWire.h>

OneWire  ds(6);  // on pin 10 (a 4.7K resistor is necessary)


AltSoftSerial altSerial;

byte addr0[8] = { 0x28, 0xBA, 0x58, 0xA9, 0x6, 0x0, 0x0, 0x96 }; //DOWN
byte addr1[8] = { 0x28, 0x5C, 0x3B, 0xAB, 0x6, 0x0, 0x0, 0x55 }; //UP
byte *sensor[2] = { addr0, addr1 };
float temp[2];
byte data[12];
time_t start;
int vent_start = 60 * 2;
int heat_time  = 60 * 4;
int vent_stop  = 60 * 8;
int again =      60 * 10;
byte state;
byte wait_count;
byte revent = 6;
float goal = 2.0;
int state_time[4] = {vent_start, again, heat_time, vent_stop};

void setup(void) {
  Serial.begin(9600);
  pinMode(4, OUTPUT);
  digitalWrite(4, LOW);
  pinMode(5, OUTPUT);
  digitalWrite(5, LOW);
  set_time();
  state = 0;
  start = now();
  altSerial.begin(9600);
}


void loop(void) {
  switch (state) {
    case 0: //vent start
      wait_count = 0;
      if (now() < start + vent_start) {
        digitalWrite(4, HIGH);
        digitalWrite(5, LOW);
      } else {
        get_temp();
        state = (temp[0] > goal)? 1: 2;
        start = now();
      }
      break;
     case 1: //wait
      if (now() < start + again) {
        digitalWrite(4, LOW);
        digitalWrite(5, LOW);
      } else {
        get_temp();
        state = (temp[0] > goal+3 and wait_count++ < revent )?1: 0;
        start = now();
      }
      break;
     case 2: //heat
      if (now() < start + heat_time) {
        digitalWrite(4, HIGH);
        digitalWrite(5, HIGH);
      } else {
        state = 3;
        start = now();
      }
      break;
     case 3: //after heat
      if (now() < start + vent_stop) {
        digitalWrite(4, HIGH);
        digitalWrite(5, LOW);
      } else {
        state = 1;
        start = now();
      }
      break;
  }
  get_temp();

  Serial.print("DOWN = ");  Serial.print(temp[0]);
  altSerial.print("D="); altSerial.print(temp[0]);
  Serial.print(" UP = ");  Serial.print(temp[1]);
  altSerial.print(" U="); altSerial.print(temp[1]);

  Serial.println();

  Serial.print("State = "); Serial.print(state);
  altSerial.print(" St="); altSerial.print(state);
  if (state == 1) {
    Serial.print(" count = "); Serial.print(wait_count);
    altSerial.print(" W="); altSerial.print(wait_count);
  }
  Serial.print(" Time = ");  Serial.print(now() - start);
  altSerial.print(" T="); altSerial.print(now() - start);

  Serial.print(" is ");  Serial.print(state_time[state]);
  altSerial.print(" is "); altSerial.print(state_time[state]);

  Serial.println();
  altSerial.println();
    
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
  delay(10000);
}

void set_time() {
  const unsigned long DEFAULT_TIME = 1357041600; // Jan 1 2013
  setTime(DEFAULT_TIME);
}

void get_temp(void) {
  byte i,sen;
  byte present = 0;

  for (sen = 0; sen < 2; sen++) {
    
    ds.reset();
    ds.select(sensor[sen]);
    ds.write(0x44, 1);        // start conversion, with parasite power on at the end
  
    delay(1000);     // maybe 750ms is enough, maybe not
    // we might do a ds.depower() here, but the reset will take care of it.
  
    present = ds.reset();
    ds.select(sensor[sen]);
    ds.write(0xBE);         // Read Scratchpad

    for ( i = 0; i < 9; i++) {           // we need 9 bytes
      data[i] = ds.read();
    }
    int16_t raw = (data[1] << 8) | data[0];
    byte cfg = (data[4] & 0x60);
    // at lower res, the low bits are undefined, so let's zero them
    if (cfg == 0x00) raw = raw & ~7;  // 9 bit resolution, 93.75 ms
    else if (cfg == 0x20) raw = raw & ~3; // 10 bit res, 187.5 ms
    else if (cfg == 0x40) raw = raw & ~1; // 11 bit res, 375 ms
    //// default is 12 bit resolution, 750 ms conversion time
    temp[sen] = (float)raw / 16.0;
  }
}

