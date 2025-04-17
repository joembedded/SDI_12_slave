/** SDI-12 Slave Boilerplate, basierend auf https://github.com/EnviroDIY/Arduino-SDI-12
Einfacher SDI12-Slave Sensor Protokoll V1.3 

Erweitert um A-Kommando fuer EEPROM, MEGA328P hat 1k EEPROM mit ca. 100k Zyklen
Guenstige ATMEGA-Booards gibts ohne CH340, Stromverbrauch ca. 15-20 mA
Concurrent Measurement rausgenommen
Arduino kann direkt an den SDI-12-Bus (hier PIN7) angeschlossen werden

Stand 17.04.2025: Es scheint so, dass Timing-Fehler dir IRQs aus der Spur bringen.
                  Gut nachvollziehbar mit dem SDI12-Term: https://github.com/joembedded/SDI12Term 
				  welches es nicht 'soo' genau mit dem Timing nimmt...

*/

#include <SDI12.h>
#include <EEPROM.h>
#include <avr/wdt.h>

#ifndef SDI12_DATA_PIN
#define SDI12_DATA_PIN 7
#endif

int8_t dataPin = SDI12_DATA_PIN; /*!< The pin of the SDI-12 data bus */
char sensorAddress = '0';        /*!< (default) address  */
int state = 0;
bool globalWithCRC = false;

#define WAIT 0
#define INITIATE_MEASUREMENT 1
#define PROCESS_COMMAND 2

// Create object by which to communicate with the SDI-12 bus on SDIPIN
SDI12 slaveSDI12(dataPin);

void pollSensor(float* measurementValues) {
  measurementValues[0] = 1.111111;
  measurementValues[1] = -2.222222;
  measurementValues[2] = 3.333333;
  measurementValues[3] = -4.444444;
  measurementValues[4] = 5.555555;
  measurementValues[5] = -6.666666;
  measurementValues[6] = 7.777777;
  measurementValues[7] = -8.888888;
  measurementValues[8] = -9.999999;
}

int set_sensor_adress(char newadr) {
  if ((newadr >= '0' && newadr <= '9') || (newadr >= 'a' && newadr <= 'z') || (newadr >= 'A' && newadr <= 'Z')) {
    sensorAddress = newadr;
    return 0;        // OK
  } else return -1;  // Invalid
}


void parseSdi12Cmd(String command, String* dValues) {
  if (command.charAt(0) != sensorAddress && command.charAt(0) != '?') { return; }
  // *todo* auf korrektes Ende ('!') pruefen
  int h;
  bool withCRC = false;
  String responseStr = "";
  if (command.length() > 1) {
    switch (command.charAt(1)) {
      case 'I':
        // Identify command
        //            "13COMPNAME0000011.0001";  // V1.3 - ID String
        responseStr = "13JOEM_TST0123456.0001";
        break;
      case 'M':
        globalWithCRC = false;
        if (command.length() > 2) {
          if (command.charAt(2) == 'C') globalWithCRC = true;
        }
        // *todo*: M1-x auswerten

        // Initiate measurement command
        // Slave should immediately respond with: "tttnn":
        //    3-digit (seconds until measurement is available) +
        //    1-digit (number of values that will be available)
        // Slave should also start a measurment but may keep control of the data line
        // until advertised time elapsed OR measurement is complete and service request
        // sent
        responseStr =
          "0219";  // 9 values ready in 21 sec; Substitue sensor-specific values here
        // It is not preferred for the actual measurement to occur in this subfunction,
        // because doing to would hold the main program hostage until the measurement is
        // complete.  Instead, we'll just set a flag and handle the measurement
        // elsewhere. It is preferred though not required that the slave send a service
        // request upon completion of the measurement.  This should be handled in the
        // main loop().
        state = INITIATE_MEASUREMENT;
        break;
        // NOTE: "aM1...9!" commands may be added by duplicating this case and adding
        //       additional states to the state flag

      case 'D':
        // Send data command
        // Slave should respond with a String of values
        // Values to be returned must be split into Strings of 35 characters or fewer
        // (75 or fewer for concurrent).  The number following "D" in the SDI-12 command
        // specifies which String to send
        h = ((int)command.charAt(2) - 48);
        if (h >= 0 && h <= 9) {
          responseStr = dValues[h];
          if (globalWithCRC) withCRC = true;
        } else responseStr = "";
        break;
      case 'A':
        // Change address command
        // Slave should respond with blank message (just the [new] address + <CR> +
        // <LF>)
        if (!set_sensor_adress(command.charAt(2))) {  // Wenn OK speichern
          EEPROM.write(0, sensorAddress);
        }
        break;
      default:  // Hier z.B. 'X'-Kommando einbauen
        // Mostly for debugging; send back UNKN if unexpected command received
        responseStr = "UNKN";
        break;
    }
  }

  // Issue the response specified in the switch-case structure above.
  String fullResponse = String(sensorAddress) + responseStr;
  slaveSDI12.sendResponse(fullResponse, withCRC);
  slaveSDI12.sendResponse("\r\n");
}

void formatOutputSDI(float* measurementValues, String* dValues, unsigned int maxChar) {
  /* Ingests an array of floats and produces Strings in SDI-12 output format */

  dValues[0] = "";
  int j = 0;

  // upper limit on i should be number of elements in measurementValues
  for (int i = 0; i < 9; i++) {
    // Read float value "i" as a String with 6 decimal digits
    // (NOTE: SDI-12 specifies max of 7 digits per value; we can only use 6
    //  decimal place precision if integer part is one digit)
    String valStr = String(measurementValues[i], 6);
    // Explictly add implied + sign if non-negative
    if (valStr.charAt(0) != '-') { valStr = '+' + valStr; }
    // Append dValues[j] if it will not exceed 35 (aM!) or 75 (aC!) characters
    if (dValues[j].length() + valStr.length() < maxChar) {
      dValues[j] += valStr;
    }
    // Start a new dValues "line" if appending would exceed 35/75 characters
    else {
      dValues[++j] = valStr;
    }
  }

  // Fill rest of dValues with blank strings
  while (j < 9) { dValues[++j] = ""; }
}

//---------------- SETUP ------------------
void setup() {
  slaveSDI12.begin();
  delay(500);
  set_sensor_adress(EEPROM.read(0));
  slaveSDI12.forceListen();  // sets SDIPIN as input to prepare for incoming message

  Serial.begin(115200);
  Serial.print("--SDI-Slave Adr.:'");
  Serial.print(sensorAddress);
  Serial.println("'--");

  wdt_enable(WDTO_8S);  // WD 8 Sekunden
}

//---------------- LOOP ------------------
void loop() {
  static float measurementValues[9];  // 9 floats to hold simulated sensor data
  static String
    dValues[10];                       // 10 String objects to hold the responses to aD0!-aD9! commands
  static String commandReceived = "";  // String object to hold the incoming command

  // Laufzeitkontrolle 
  static unsigned long lastMillis;
  unsigned long currentMillis = millis();
  if(currentMillis > lastMillis+1000 ){
        lastMillis = currentMillis;
         Serial.print("*");
  }
  wdt_reset();

  // If a byte is available, an SDI message is queued up. Read in the entire message
  // before proceding.  It may be more robust to add a single character per loop()
  // iteration to a static char buffer; however, the SDI-12 spec requires a precise
  // response time, and this method is invariant to the remaining loop() contents.
  int avail = slaveSDI12.available();
  if (avail < 0) {
    slaveSDI12.clearBuffer();
  }  // Buffer is full; clear
  else if (avail > 0) {
    for (int a = 0; a < avail; a++) {
      char charReceived = slaveSDI12.read();
      // Character '!' indicates the end of an SDI-12 command; if the current
      // character is '!', stop listening and respond to the command
      if (charReceived == '!') {
        state = PROCESS_COMMAND;
        // Command string is completed; do something with it
        parseSdi12Cmd(commandReceived, dValues);

        // Debug
        {
          Serial.print("Rec:'");
          Serial.print(commandReceived);
          Serial.println("'");
        }

        // Clear command string to reset for next command
        commandReceived = "";
        // '!' should be the last available character anyway, but exit the "for" loop
        // just in case there are any stray characters
        slaveSDI12.clearBuffer();
        // eliminate the chance of getting anything else after the '!'
        slaveSDI12.forceHold();
        break;
      }
      // If the current character is anything but '!', it is part of the command
      // string.  Append the commandReceived String object.
      else {
        // Append command string with new character
        commandReceived += String(charReceived);
      }
    }
  }


  // For aM! and aC! commands, parseSdi12Cmd will modify "state" to indicate that
  // a measurement should be taken
  switch (state) {
    case WAIT:
      {
        break;
      }
    case INITIATE_MEASUREMENT:
      {
        // Do whatever the sensor is supposed to do here
        // For this example, we will just create arbitrary "simulated" sensor data
        // NOTE: Your application might have a different data type (e.g. int) and
        //       number of values to report!
        pollSensor(measurementValues);
        // Populate the "dValues" String array with the values in SDI-12 format
        formatOutputSDI(measurementValues, dValues, 35);
        // For aM!, Send "service request" (<address><CR><LF>) when data is ready
        String fullResponse = String(sensorAddress) + "\r\n";
        slaveSDI12.sendResponse(fullResponse);
        state = WAIT;
        slaveSDI12.forceListen();  // sets SDI-12 pin as input to prepare for incoming
                                   // message AGAIN
        break;
      }
    case PROCESS_COMMAND:
      {
        state = WAIT;
        slaveSDI12.forceListen();
        break;
      }
  }
}
