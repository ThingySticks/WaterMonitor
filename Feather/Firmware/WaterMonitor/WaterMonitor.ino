// A7 === D9 - d9 Is used by hot flow.
#define VBAT_PIN A7

#define FLOW_SENSOR_ENABLED_PIN 5
#define COLD_FLOW_PULSE_PIN 6
#define HOT_FLOW_PULSE_PIN 9
#define TEMPERATURE_DQ_PIN 10
#define COLD_VALVE_PIN 11
#define HOT_VALVE_PIN 12

#define EXTERNAL_VOLTAGE_SENSE_PIN A5 
#define LEAK_SENSE_FAULT_PIN A2
#define LEAK_SENSE_2_PIN A1
#define LEAK_SENSE_1_PIN A0


int coldPulseCount = 0;
int hotPulseCount = 0;
int lastColdPulseCount = 0;
int lastHotPulseCount = 0;
int counter = 0;
bool hotValveEnabled = false;
bool coldValveEnabled = false;
bool sensorPowerEnabled = false;

// how long to delay between loops.
int loopDelay = 1000;

float inputVoltage = 0;
float leakSenseFault  = 0;
float leakSense2 = 0;
float leakSense1 = 0;
float measuredvbat = 0;

// How long (number of loops) the flow indicator should be on for..
int indicatorOnCount = 0;


void setup() {

  Serial.begin(115200);
  Serial1.begin(115200);
  
  pinMode(LED_BUILTIN, OUTPUT);
  
  pinMode(FLOW_SENSOR_ENABLED_PIN, OUTPUT); 
  pinMode(COLD_FLOW_PULSE_PIN, INPUT);
  pinMode(HOT_FLOW_PULSE_PIN, INPUT); 
  pinMode(TEMPERATURE_DQ_PIN, INPUT); 
  pinMode(COLD_VALVE_PIN, OUTPUT); 
  pinMode(HOT_VALVE_PIN, OUTPUT);

  pinMode(EXTERNAL_VOLTAGE_SENSE_PIN, INPUT); 
  pinMode(LEAK_SENSE_FAULT_PIN, INPUT); 
  pinMode(LEAK_SENSE_2_PIN, INPUT);
  pinMode(LEAK_SENSE_1_PIN, INPUT);

  // Enable the sensors
  setSensorPower(true);
  
  // Small delay to let any noise settle down.
  delay (200);
  
  // Attach interrupts to the pulse pins to count rotation (and hence flow).
  attachInterrupt(COLD_FLOW_PULSE_PIN, coldPulseIsr, FALLING);
  attachInterrupt(HOT_FLOW_PULSE_PIN, hotPulseIsr, FALLING);

  // Set the valves to open.
  setHotValve(true);
  setColdValve(true);
}

void loop() {
  checkForFlow();
  
  readAdcs();

  printFlowDetails();

  setFlowIndicator();

  checkSerialCommands();

  delay(loopDelay);
  counter++;
}

void readAdcs() {

  // Check the External DC input voltage.
  inputVoltage = analogRead(EXTERNAL_VOLTAGE_SENSE_PIN); // V-Sense
  inputVoltage *= 5.3; // (10/43k pair)
  inputVoltage *= 3.3; // Correct for 3.3V reference.
  inputVoltage /= 1024;
  
  // If no leak sensor is connected these will be all over the place.
  // fault pin might need to be modified to be power to the leak sensor.
  leakSenseFault = analogRead(LEAK_SENSE_FAULT_PIN);
  leakSense2 = analogRead(LEAK_SENSE_2_PIN);
  leakSense1 = analogRead(LEAK_SENSE_1_PIN);
}

void printFlowDetails() {
  Serial1.print("Counter: ");
  Serial1.print(counter, DEC);
  Serial1.print(", Cold: ");
  Serial1.print(coldPulseCount, DEC);
  Serial1.print(", Hot: ");
  Serial1.print(hotPulseCount, DEC);
  Serial1.print(", inputVoltage: " ); 
  Serial1.print(inputVoltage);
  Serial1.print(", leakSenseFault: " ); 
  Serial1.print(leakSenseFault);
  Serial1.print(", leakSense2: " ); 
  Serial1.print(leakSense2);
  Serial1.print(", leakSense1: " ); 
  Serial1.print(leakSense1);
  Serial1.print(", hotValveEnabled: " ); 
  Serial1.print(hotValveEnabled);
  Serial1.print(", coldValveEnabled: " ); 
  Serial1.print(coldValveEnabled);
  Serial1.print(", sensorPowerEnabled: " ); 
  Serial1.print(sensorPowerEnabled);
  Serial1.print(", loopDelay: " ); 
  Serial1.print(loopDelay);
  Serial1.print(",ms, loopDelay: " ); 
  Serial1.println();
}

void checkForFlow() {
  // Check if water is flowing, if so, set the indicator on.
  if (lastColdPulseCount != coldPulseCount) {
    indicatorOnCount = 5;
  }
  
  if (lastHotPulseCount != hotPulseCount) {
    indicatorOnCount = 5;
  }

  lastColdPulseCount = coldPulseCount;
  lastHotPulseCount = hotPulseCount;
}

void setFlowIndicator() {
  indicatorOnCount--;
  
  if (indicatorOnCount >=0 ) {
    digitalWrite(LED_BUILTIN, HIGH);
  } else {
    digitalWrite(LED_BUILTIN, LOW);
    indicatorOnCount = 0;
  }
}

// Check for a command from the serial port.
// Very basic, single character only.
void checkSerialCommands() {
  while (Serial1.available()) {
    char command = Serial1.read();

    switch (command) {
      case 'H': // Switch ON the Hot valve
        setHotValve(true);
        break;
      case 'C': // Switch ON the Cold value
        setColdValve(true);
        break;
      case 'h': 
        setHotValve(false);
        break;
      case 'c':
        setColdValve(false);
        break;
      case 'P':
        setSensorPower(true);
        break;
      case 'p':
        setSensorPower(false);
        break;
      case 'f': // "fast" refresh
        loopDelay = 1000;
        break;
      case 's': // "slow" refresh
        loopDelay = 10000;
        break;
    }
  }
}


// Set the Hot valve state (true = open, false = closed);
void setHotValve(bool valveOpen) {
  digitalWrite(HOT_VALVE_PIN, valveOpen); 
  hotValveEnabled = valveOpen;

}

// Set the Cold valve state (true = open, false = closed);
void setColdValve(bool valveOpen) {
  digitalWrite(COLD_VALVE_PIN, valveOpen);
  coldValveEnabled = valveOpen;
}

void setSensorPower(bool enabled) {
  digitalWrite(FLOW_SENSOR_ENABLED_PIN, enabled); 
  sensorPowerEnabled = enabled;
}


void coldPulseIsr() {
  // low pulse caused the interrupt, ensure it's still low.
  // very basic debounce...
  if (!digitalRead(6)) {
    coldPulseCount++;
    indicatorOnCount = 3;
    setFlowIndicator();
  }
}
  
void hotPulseIsr() {
  if (!digitalRead(9)) {
    hotPulseCount++;
    indicatorOnCount = 3;
    setFlowIndicator();
  }
}

