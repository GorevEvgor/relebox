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
#define delta 0.5

#define LOG_BUF 100
#define LOG_TIME 30*60

typedef struct log_t
  {
    time_t time;
    byte type;
    byte data[8];
  };

typedef struct VH_t
  {
//    char *name;
    byte vent;
    byte heat;
    int wait;
    int count;
    time_t last;
  };

typedef struct sens_t
  {
    byte addr[8];
    float temp;
  };

VH_t VH_param[4] =
{
  {  //0 wait
//    "wait", 
    LOW, LOW, 60 * 10, 0, 0
  },
  {  //1 start vent
//    "vent1",   // name
    HIGH,      // vent
    LOW,      // heat
    60 * 2,  // wait
    0,        //count
    0          // last
  },
  {  //2 heat
//    "heat", 
    HIGH, HIGH, 60 * 4, 0, 0
  },
  {  //3 post vent
//    "vent2", 
    HIGH, LOW, 60 * 8, 0, 0
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
byte debug_show = 1;

log_t logRecord[LOG_BUF];
int  logIndex = 0;
int logStart = 0;
time_t  logLast = 0;

OneWire  ds(SENS_pin);  // 4.7K resistor is necessary
AltSoftSerial altSerial; // pin 8 9


void setup(void) {

  int monitor_time = 30;

  Serial.begin(9600);
  altSerial.begin(9600);

  pinMode(VENT_pin, OUTPUT);
  pinMode(HEAT_pin, OUTPUT);
  pinMode(LED_pin, OUTPUT);
  pinMode(CD_pin, INPUT);

  getTemp();
  setState(1);
  
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
  byte i;  
  
  digitalWrite(LED_pin,(error)? HIGH: LOW);

  switch (state) {
     case 0: //wait
      if (now() > start + VH_param[state].wait) {
        getTemp();
        setState((( sensor[0].temp < goal + delta ) or 
                  ( wait_count++ > revent ) or 
                  ( now() < VH_param[2].last + 3600 ))? 1:0 ); 
      }
      break;
    case 1: //vent start
      wait_count = 0;
      if (now() > start + VH_param[state].wait) {
        getTemp();
        setState((sensor[0].temp > goal or ((error & 0x3) == 0x3) )? 0: 2);
      }
      break;
     case 2: //heat
      if (now() > start + VH_param[state].wait) setState(3);
      break;
     case 3: //after heat
      if (now() > start + VH_param[state].wait) setState(0);
      break;
  }

  for (i=0 ; i < 10; i++) {
    delay(1000);
    if (altSerial.available() ) {
      dialog();
      altSerial.flush();
    };
  };

  if (now() > logLast + LOG_TIME) {
    getTemp();
    logAddT1();
    logLast=now();
  }
  
  print_data();

}

void dialog() {
  char c[2];
  byte l;
  
  altSerial.setTimeout(100);
  
  l = altSerial.readBytes(c,2);
  if (l < 2) return;
  if (c[0]=='t') {
    if (c[1]=='g') get_Time();
    else 
      if (c[1]=='s') set_Time(); 
  };
  if (c[0]=='s') {
    if (c[1]=='g') get_State();
    else 
      if (c[1]=='s') set_State(); 
  };
  if (c[0]=='d') {
    if (c[1]=='s') debug_show = 1;
    else 
      if (c[1]=='n') debug_show = 0; 
  };
  if (c[0]=='l') {
    if (c[1]=='s') log_Show();
    else 
      if (c[1]=='c') log_Clear(); 
  };
};

void set_State() {
  byte l;
  if (! altSerial.available() ) return;
  l = altSerial.read();
  if ( isDigit(l) ) l -= 0x30; else return;
  if ( l>=0 and l<4 ) {
    getTemp();
    setState(l);
  };
  get_State();
}

void get_State() {
    getTemp();
    get_Time();
    altSerial.print("D="); altSerial.print(sensor[0].temp);
    altSerial.print(" U="); altSerial.print(sensor[1].temp);
    altSerial.print(" St="); altSerial.print(state);
    altSerial.print(" W="); altSerial.print(wait_count);
    altSerial.print(" T="); altSerial.print(now() - start);
    altSerial.print("/"); altSerial.print(VH_param[state].wait);
    altSerial.print(" E="); altSerial.print(error);
    altSerial.print(" V="); altSerial.print(VH_param[0].count);
    altSerial.print(" H="); altSerial.print(VH_param[2].count);
    altSerial.println();
}

void get_Time() {
    time_t ctime;

    ctime=now();
    altSerial.print(year(ctime));
    altSerial.print("-");
    altSerial.print(month(ctime));
    altSerial.print("-");
    altSerial.print(day(ctime));
    altSerial.print(" ");
    altSerial.print(hour(ctime));
    altSerial.print(":");
    altSerial.print(minute(ctime));
    altSerial.print(":");
    altSerial.print(second(ctime));
    altSerial.println();
}

void set_Time() {
  byte l;
  char data[2];
  int m_year,m_month,m_day,m_hour,m_min,m_sec;

  l = altSerial.readBytes(data,2);
  if (99 == (m_year=to_d(l,data))) return;
  l = altSerial.readBytes(data,2);
  if (99 == (m_month=to_d(l,data))) return;
  l = altSerial.readBytes(data,2);
  if (99 == (m_day=to_d(l,data))) return;
  l = altSerial.readBytes(data,2);
  if (99 == (m_hour=to_d(l,data))) return;
  l = altSerial.readBytes(data,2);
  if (99 == (m_min=to_d(l,data))) return;
  l = altSerial.readBytes(data,2);
  if (99 == (m_sec=to_d(l,data))) return;
  setTime(m_hour,m_min,m_sec,m_day,m_month,m_year);

  get_Time();

    getTemp();
    logAddT1();
    logLast=now();

}

byte to_d (byte l, char *b) {
  byte val = 0;
  if (l<1) return 99;
  if (isDigit(b[0])) val = b[0] - 0x30;
  if (l>1 and isDigit(b[1])) val = val * 10 + (b[1] - 0x30);
  return val;
}

void print_data() {
  byte i;
  byte cd = 1;
  if (! debug_show) return;
  for ( i = 0; i < 4; i++) {
    cd &= digitalRead(CD_pin);
    delay(100);
  }
      
  if (!cd and !debug) return;
  
  getTemp();
  if (debug) {
    Serial.print(" logStart=");  Serial.print(logStart);
    Serial.print(" logIndex=");  Serial.print(logIndex);
    Serial.print(" now="); Serial.print(now());
    Serial.print(" logLast=");Serial.print(logLast);
    Serial.println();
  }
  if (cd) {
    altSerial.print("D="); altSerial.print(sensor[0].temp);
    altSerial.print(" U="); altSerial.print(sensor[1].temp);
    altSerial.print(" St="); altSerial.print(state);
    altSerial.print(" W="); altSerial.print(wait_count);
    altSerial.print(" T="); altSerial.print(now() - start);
    altSerial.print("/"); altSerial.print(VH_param[state].wait);
    altSerial.print(" E="); altSerial.print(error);
    altSerial.print(" V="); altSerial.print(VH_param[0].count);
    altSerial.print(" H="); altSerial.print(VH_param[2].count);
    altSerial.println();
  }

}

void getTemp(void) {
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

void logAddRec (struct log_t *rec) {
  logRecord[logIndex] = *rec;
  logIndex++;
  if (logIndex >= LOG_BUF) logIndex=0;
  if (logIndex == logStart) {
    logStart++;
    if (logStart >= LOG_BUF) logStart=0;
  };
};

void logAddT1(void) {
  log_t r;
  
  r.time = now();
  r.type = 1;
  r.data[0] = byte(sensor[0].temp*10);
  r.data[1] = byte(sensor[1].temp*10);
  r.data[2] = state;
  r.data[3] = error;
  r.data[4] = VH_param[0].count;
  r.data[5] = VH_param[1].count;
  r.data[6] = VH_param[2].count;
  r.data[7] = VH_param[3].count;

  logAddRec(&r);
};

void log_Show() {
  int i;
  i = logStart;
  if (i > logIndex) {
    while (i < LOG_BUF) {
      if (logRecord[i].type == 1) log_ShowT1(&logRecord[i]);
      i++;
    }
    if (i == LOG_BUF) i = 0;
  }
  while (i < logIndex) {
    if (logRecord[i].type == 1) log_ShowT1(&logRecord[i]);
    i++;
  }
}

void log_ShowT1(struct log_t *rec) {
    altSerial.print(year(rec->time));
    altSerial.print("-");
    altSerial.print(month(rec->time));
    altSerial.print("-");
    altSerial.print(day(rec->time));
    altSerial.print(" ");
    altSerial.print(hour(rec->time));
    altSerial.print(":");
    altSerial.print(minute(rec->time));
    altSerial.print(":");
    altSerial.print(second(rec->time));
    altSerial.print(" t");
    altSerial.print(rec->type);
    altSerial.print(" D"); altSerial.print(rec->data[0]/10.0);
    altSerial.print(" U"); altSerial.print(rec->data[1]/10.0);
    altSerial.print(" S"); altSerial.print(rec->data[2]);
    altSerial.print(" E"); altSerial.print(rec->data[3]);
    altSerial.print(" W"); altSerial.print(rec->data[4]);
    altSerial.print(" V"); altSerial.print(rec->data[5]);
    altSerial.print(" H"); altSerial.print(rec->data[6]);
    altSerial.println();
}

void log_Clear() {
  logStart=logIndex;
}
