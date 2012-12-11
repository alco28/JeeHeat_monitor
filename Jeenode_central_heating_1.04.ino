/*
  JeeHeat LowPower Temperature  
 
  Example for using Jeenode (v6) to monitor central house Heatingsystem. with jeenode connected to two AA batteries connected to 3.3V rail. 
  Voltage regulator must defore not be fitted Jumper between PWR and Dig7 on JeePort 4
  This setup allows the DS18B20 to be switched on/off by turning Dig7 HIGH/LOW

--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
Current consumption:  (amperé measurement done by Alco on this Jeeheater)
  WHEN IN OPERATION CURRENT CONSUMPTION @ 3.315 V = 6.89mA (peak 15mA)
  WHEN SLEEPING                                   = 113uA

--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
   
 Can be used as a part of the openenergymonitor.org project systems
 Licence: GNU GPL V3
 
  Author: Alco van Neck 
  Builds upon JeeLabs RF12 library and Arduino IDE

  THIS SKETCH REQUIRES:

  Non default Libraries to place into the standard arduino libraries folder:
	- JeeLib		https://github.com/jcw/jeelib
	- OneWire library	http://www.pjrc.com/teensy/td_libs_OneWire.html
	- DallasTemperature	http://download.milesburton.com/Arduino/MaximTemperature
--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
*/

//INTIALISATION
//Libaries
#include <JeeLib.h>   
#include <Ports.h> // jeenode ports numbers
#include <avr/sleep.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#define SERIAL    1 // set to 1 to also report readings on the serial port
#define DEBUG     1 // set to 1 to display 0;

// scheduler tasks =============================================

// The scheduler makes it easy to perform various tasks at various times:
  enum { MEASURE, REPORT, TASK_END }; 
  static word schedbuf[TASK_END]; 
  Scheduler scheduler (schedbuf, TASK_END);
  static byte reportCount;                                                // count up until next report, i.e. packet send

//Define measure periods (for debuging, short timings 24 sec for a loop)
  #if DEBUG
      #define MEASURE_PERIOD   60                                            // how often to measure, in tenths of seconds
      #define REPORT_EVERY     4                                             // report every N measurement cycles on Radio (MEASURE_PERIOD * REPORT_EVERY = length of loop)
      #define SMOOTH           2                                              // smoothing factor used for running averages of sensorreadings
    #else
      #define MEASURE_PERIOD   300                                            // how often to measure, in tenths of seconds (default: 300 msec.)
      #define REPORT_EVERY     3                                             // report every N measurement cycles on Radio (MEASURE_PERIOD * REPORT_EVERY = length of loop, default 3x300msec=90 sec)
      #define SMOOTH           3                                              // smoothing factor used for running averages of sensorreadings
  #endif 

// ACK package receiving &ACK timings
  #define RETRY_PERIOD   25   // how soon to retry if ACK didn't come in (ms)
  #define RETRY_LIMIT    4    // maximum number of times to retry
  #define ACK_TIME       500  // number of milliseconds to wait for an ack

// Init Radio =============================================
#define freq RF12_868MHZ                                                // Frequency of RF12B module can be RF12_433MHZ, RF12_868MHZ or RF12_915MHZ. You should use the one matching the module you have.
const int nodeID = 10;                                                  // JeeHeat temperature RFM12B node ID - should be unique on network
const int networkGroup = 210;                                           // JeeHeat RFM12B wireless network group - needs to be same as emonBase and emonGLCD
#define RADIO_SYNC_MODE 2                                              // set the sync mode to 2 if the fuses are still the Arduino default mode 3 (full powerdown) can only be used with 258 CK startup fuses

ISR(WDT_vect) { Sleepy::watchdogEvent(); }                              // Attached JeeLib sleep function to Atmega328 watchdog -enables MCU to be put into sleep mode inbetween readings to reduce power consumption 

// Init sensors =============================================
#define ONE_WIRE_BUS 7                                                  // Data wire from DS18B20 tempsensor is plugged into port 7 on the Arduino or port 4 of jeenode
OneWire oneWire(ONE_WIRE_BUS);                                          // Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
DallasTemperature sensors(&oneWire);                                    // Pass our oneWire reference to Dallas Temperature.

const int sensorResolution_T1 = 10;                                     //DS18B20 resolution 9,10,11 or 12bit corresponding to (0.5, 0.25, 0.125, 0.0625 degrees C LSB), lower resolution means lower power
const int sensorResolution_T2 = 10;  
const int sensorResolution_T3 = 10;  

// See the tutorial on how to obtain these addresses: http://www.hacktronics.com/Tutorials/arduino-1-wire-address-finder.html
DeviceAddress address_T1 = { 0x28, 0xA3, 0xC0, 0x41, 0x04, 0x00, 0x00, 0xC8 };    // set DS18B20 hardware address sensors 1
DeviceAddress address_T2 = { 0x28, 0xC7, 0x88, 0x11, 0x04, 0x00, 0x00, 0xD8 };    //sensor 2
DeviceAddress address_T3 = { 0x28, 0x54, 0xAB, 0x41, 0x04, 0x00, 0x00, 0x5E };    //sensor 3

//************* payload**********************************
// array to hold the sensor output and sending it.
typedef struct {
  	  int T_HW;    // hotwater
	  int T_HO;    // heater_out
          int T_HI;    // heater_in	  
	  int JH_B;    // battery
          int JH_R;    // ACK retries
          	                                      
} Payload;
Payload JeeHeat; // array Alias

// utility code to perform simple smoothing as a running average of sensorreadings
static int smoothedAverage(int prev, int next, byte firstTime =0) {
    if (firstTime)
        return next;
    return ((SMOOTH - 1) * prev + next + SMOOTH / 2) / SMOOTH;
}
//*****************************************************


static void serialFlush () {
    #if ARDUINO >= 100
        Serial.flush();
    #endif  
    delay(2); // make sure tx buf is empty before going back to sleep
}

// ********* scheduled tasks here ==================================!!!!!

//MEASURE cycle

static void doMeasure () {

    byte firstTime = JeeHeat.T_HW == 0; // special case to init running avg
  
    digitalWrite(7,HIGH);                                                    // turn on DS18B20
    delay(10);                                                                // Allow 10ms for the sensor to be ready
    sensors.requestTemperatures();                                            // Send the command to get temperatures
    float T1, T2 , T3;
    T1 = sensors.getTempC(address_T1) 	        * 100;                        //read-out sensor 1 (*100 for resolution)
    T2 = sensors.getTempC(address_T2) 	        * 100;                        //read-out sensor 2 (*100 for resolution)
    T3 = sensors.getTempC(address_T3) 	        * 100;                        //read-out sensor 3 (*100 for resolution)
    digitalWrite(7,LOW); 		                                      // turn OFF DS18B20 sensors for battery saving
    
    // !!! Calibration correction for sensors !!!
    int Temp1 = T1 - 150, Temp2 = T2 , Temp3 = T3 - 100;                      // my sensors need an calibration due isolation factor of the housing where I put them in.
    
    JeeHeat.T_HW = smoothedAverage (JeeHeat.T_HW, Temp1, firstTime);
    JeeHeat.T_HI = smoothedAverage (JeeHeat.T_HI, Temp2, firstTime);
    JeeHeat.T_HO = smoothedAverage (JeeHeat.T_HO, Temp3, firstTime);
    
  // Some powersaving stuff here  
  bitClear(PRR, PRADC);        // power up the ADC  
  ADCSRA |= bit(ADEN);         // enable the ADC  
  delay(10);                   // stabilise the voltage
  
  JeeHeat.JH_B=readVcc();     // read-out battery script (see below)
  
  ADCSRA &= ~ bit(ADEN);      // disable the ADC  
  bitSet(PRR, PRADC);         // power down the ADC
  
    
} //end doMeasure


//---------------------------------------------------------------------------------------------------------------------------------------------------
// REPORT Cycle

static void doReport() {
    rf12_sleep(RF12_WAKEUP);// activate RF module out of sleep

    #if SERIAL // print-out on serial port
 
        Serial.println(" ");Serial.print("Temps: ");
        Serial.print((int) JeeHeat.T_HW);           Serial.print(" ");          // hotwater tap
        Serial.print((int) JeeHeat.T_HI);           Serial.print(" ");          // heater watertemp in
        Serial.print((int) JeeHeat.T_HO);           Serial.print(" Batt: ");    // heater watertemp out
        Serial.print((int) JeeHeat.JH_B);           Serial.println(" ");        // Jeeheater battery voltage reading
        serialFlush();
    #endif
    
    JeeHeat.JH_R = 0;
  
   
    for (byte i = 0; i < RETRY_LIMIT; ++i) {
        JeeHeat.JH_R = (int)i;
        while(!rf12_canSend())
        rf12_recvDone();
        Sleepy::loseSomeTime(50);
        rf12_sendStart(RF12_HDR_ACK, &JeeHeat, sizeof JeeHeat);
        rf12_sendWait(RADIO_SYNC_MODE);
        byte acked = waitForAck(); 
        rf12_sleep(RF12_SLEEP);  //put RF module back to sleep
 
           if(acked) {
                 #if  DEBUG
                 Serial.println("");
                 Serial.print("ACK in ");
                 Serial.print((int) i);
                 Serial.print(" times");
                 serialFlush();
                  #endif
                 scheduler.timer(MEASURE, MEASURE_PERIOD);
                 return;
                 }
 
       Sleepy::loseSomeTime(RETRY_PERIOD * 10);
           }

    scheduler.timer(MEASURE, MEASURE_PERIOD);
    #if DEBUG
      Serial.println(" no ack!");
      serialFlush();
    #endif
    
    } // end doReport


//*****************************************************************************************************************************

// readout battery voltage
long readVcc() {
  long result; 
  ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  delay(2);
  ADCSRA |= _BV(ADSC);
  while (bit_is_set(ADCSRA,ADSC));
  result = ADCL;
  result |= ADCH<<8;
  result = 1126400L / result;
  return result;
 
}



  // wait a few milliseconds for proper ACK to me, return true if indeed received
static byte waitForAck() {
    MilliTimer ackTimer;
    while (!ackTimer.poll(ACK_TIME)) {
        if (rf12_recvDone() && rf12_crc == 0 &&
                // see http://talk.jeelabs.net/topic/811#post-4712
                rf12_hdr == (RF12_HDR_DST | RF12_HDR_CTL | nodeID))
            return 1;
        set_sleep_mode(SLEEP_MODE_IDLE);
       // set_sleep_mode(SLEEP_MODE_PWR_DOWN);
        sleep_mode();
    }
    return 0;
}



//=====================================SETUP===============================================================================================================================================================================

void setup() {
  
  #if SERIAL || DEBUG
  Serial.begin(57600);
  Serial.println("Jeenode heating monitor"); 
  rf12_config(0);
  Serial.print("Node: "); 
  Serial.print(nodeID); 
  Serial.print(" Freq: "); 
  if (freq == RF12_433MHZ) Serial.print("433Mhz");
  if (freq == RF12_868MHZ) Serial.print("868Mhz");
  if (freq == RF12_915MHZ) Serial.print("915Mhz"); 
  Serial.print(" Network: "); 
  Serial.println(networkGroup);
  serialFlush();
    #else 
     rf12_config(0); // don't report info on the serial port (=0) and read settings from EEPROM (set it with RM12_Demo sketch from jeelib)
  #endif 
  
     // setup radiochip
      rf12_control(0xC040);                                                 // set low-battery level to 2.2V i.s.o. 3.1V
      delay(10);
      rf12_sleep(RF12_SLEEP);
  
  // powerdown some stuff that whe don't use now
  PRR = bit(PRTIM1);         // only keep timer 0 going  
  ADCSRA &= ~ bit(ADEN);     // Disable the ADC  
  bitSet (PRR, PRADC);       // Power down ADC  
  bitClear (ACSR, ACIE);     // Disable comparitor interrupts  
  bitClear (ACSR, ACD);      // Power down analogue comparitor  


//================TEMP SENSORS============================
  pinMode(7,OUTPUT);                                                    // DS18B20 power control pin - see jumper setup instructions above
  digitalWrite(7,HIGH);                                                 // turn on DS18B20
   delay(10);
  
  sensors.begin();
  sensors.setResolution(address_T1, sensorResolution_T1);
  sensors.setResolution(address_T2, sensorResolution_T2);
  sensors.setResolution(address_T3, sensorResolution_T3);

 #if DEBUG
 Serial.print("Temperature Sensor Resolution: ");
 Serial.print("T1: "); if ((sensors.getResolution(address_T1), DEC) == 9)  Serial.print("0.50 C  ");
                       if((sensors.getResolution(address_T1), DEC) == 10)  Serial.print("0.25 C ");
                       if((sensors.getResolution(address_T1), DEC) == 11)  Serial.print("0.125 C ");
                       if((sensors.getResolution(address_T1), DEC) == 12)  Serial.print("0.0625 C ");
 Serial.print("T2: "); if ((sensors.getResolution(address_T2), DEC) == 9)  Serial.print("0.50 C  ");
                       if((sensors.getResolution(address_T2), DEC) == 10)  Serial.print("0.25 C ");
                       if((sensors.getResolution(address_T2), DEC) == 11)  Serial.print("0.125 C ");
                       if((sensors.getResolution(address_T2), DEC) == 12)  Serial.print("0.0625 C ");
 Serial.print("T3: "); if ((sensors.getResolution(address_T3), DEC) == 9)  Serial.print("0.50 C  ");
                       if((sensors.getResolution(address_T3), DEC) == 10)  Serial.print("0.25 C ");
                       if((sensors.getResolution(address_T3), DEC) == 11)  Serial.print("0.125 C ");
                       if((sensors.getResolution(address_T3), DEC) == 12)  Serial.print("0.0625 C ");
 Serial.println(" ");
 #endif 

reportCount = REPORT_EVERY;     // report right away for easy debugging
scheduler.timer(MEASURE, 0);    // start the measurement loop going
    
    


 } //end of setup
 
//=====================================LOOP=========================

void loop()
{ 
  #if DEBUG
        Serial.print('.');
        serialFlush();
    #endif
  
switch (scheduler.pollWaiting()) {

        case MEASURE:
            // reschedule these measurements periodically
            scheduler.timer(MEASURE, MEASURE_PERIOD);
    
            doMeasure();

            // every so often, a report needs to be sent out
            if (++reportCount >= REPORT_EVERY) {
                reportCount = 0;
                scheduler.timer(REPORT, 0);
            }
            break;
            
        case REPORT:
            doReport();
            break;
    }


} // end loop




