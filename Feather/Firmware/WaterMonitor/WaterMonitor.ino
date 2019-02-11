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

// Maximum number of sensors.
#define SENSOR_COUNT 2

// How often to read the sensor value and 
// compute flows.
#define UPDATE_INTERVAL 1000

// How often to publish the usage summary
#define PUBLISH_SUMMARY_INTERVAL 60000

// How often to compute and publish the daily stats
// 24 hours * 60 minutes * 60 seconds * 1000 milliseconds
#define DAILY_STATS_INTERVAL 86400000

/*
 * Flowrate computations:
 * Flow sensor pulses approximatly 560 times per Litre (see flowPulsesPerLitre)
 * Sensor can measure 1-30 Litres per minute.
 * 
 * Min Flow:
 * 1 Litre  = 560 pulses (per minute at min flow)
 *          => 9.3 pulses per second.(i.e. 9.3Hz) (100ms interval)
 *          
 * Max Flow
 * 30 Litre = 16,800 pulses (per minute at max flow)
 *          => 280 pulses per second (i.e. 280Hz) (3.5ms interval)
 * 
 * Measurents are taken every "Interval" (i.e. per second or per 10s or per minute or whatever...)
 * 
 * sensorPulseCount - is updated by the interrupts and cleared every interval
 * intervalSensorPulseCount - holds the pulses from the sensor this interval.
 * totalSensorPulseCount - holds the total pulse count since reset.
 * 
 * totalConsumedLitres - the total volume consumed since reset.
 * currentConsumedLitres - The volume consumed since flow stared. Reset when flow stops.
 * lastConsumedLitres - volume consumed for the last flow (set when flow stops).
 * 
 * 
 */

// The number of pulses from the flow sensor per litre of water.
// This will need to be calibrated.
float flowPulsesPerLitre[] = {560, 560};

// Names for sensors.
String sensorNames[] = {"Shower Cold", "Hot"};

bool sensorPowerEnabled = false;

// Sensor pulse count per loop.
// this is updated by the interupts
// and reset by the interval
volatile int sensorPulseCount[SENSOR_COUNT];

// Sensor pulse count this measuremenbt interval.
int intervalSensorPulseCount[SENSOR_COUNT];

// Total (non reset) sensor count.
long int totalSensorPulseCount[SENSOR_COUNT];

// If their is flow...
volatile bool hasFlow = false;

// Valve control (only on V2 PCB).
bool hotValveEnabled = false;
bool coldValveEnabled = false;

// **********************************************************
// milliseconds when to next take the interval slice for 
// flow readings.
long nextIntervalAt = 0;
// When to next publish the summary (i.e. every minute...)
long nextPublishedSummaryAt = 0;
// Milliseconds when the next day starts for stats.
long nextDayAt = 0;

// Analog inputs.
float inputVoltage = 0;
float leakSenseFault  = 0;
float leakSense2 = 0;
float leakSense1 = 0;
float measuredvbat = 0;

// How long (number of intervals) the flow indicator should (remain) be on for..
int indicatorOnCount = 0;

// The total number of litres consumed (per sensor).
float totalConsumedLitres[] = {0,0};

// the volume consumed today

// The current number of litres consumed (reset when flow stops.
float currentConsumedLitres[] = {0,0};

// Consumption within the last 24 hours, per sensor.
float todayTotalConsumedLitres[] = {0,0};
float yesterdaysTotalConsumedLitres[] = {0,0};

// The last volume consumed (set when flow has set).
float lastConsumedLitres[] = {0,0};

// The current flow rate 
float currentFlowRate[] = {0,0}; 

void setup() {

  Serial.begin(115200);
  Serial1.begin(115200);
  // Small delay to allow for the serial monitor to attach.
  delay(5000);
  
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
  // TODO: Other sensors...

  // Set the valves to open.
  setHotValve(true);
  setColdValve(true);

  // Next interval handle due in 1 second.
  long now = millis(); // expect 0.
  
  nextIntervalAt = now + UPDATE_INTERVAL;
  
  // Next per minute stats publish due in 60 seconds.
  nextPublishedSummaryAt = now + PUBLISH_SUMMARY_INTERVAL;

  // 24 hours * 60 minutes * 60 seconds * 1000 milliseconds
  nextDayAt = now + DAILY_STATS_INTERVAL;

  Serial.println("Water Monitor Initialised.");
  Serial1.println("Water Monitor Initialised.");
}

void loop() {

  if (millis() > nextIntervalAt) {
    nextIntervalAt = millis() + UPDATE_INTERVAL;
    processFlows();   
  }

  if (millis() > nextPublishedSummaryAt) {
    nextPublishedSummaryAt = millis() + PUBLISH_SUMMARY_INTERVAL;
    printWaterUsageSummary();
  }

  if (millis() > nextDayAt) {
    nextDayAt = millis() + DAILY_STATS_INTERVAL;
    updateDailyStats();
  }

  checkSerialCommands();
  
  delay(20);
}

// Called every process flow interval.
void processFlows() {
    // Update sensor pulse counts.
    for (int i=0; i<SENSOR_COUNT; i++) {
      intervalSensorPulseCount[i] = sensorPulseCount[i];
      totalSensorPulseCount[i]+=sensorPulseCount[i];
      sensorPulseCount[i] = 0;
    }

    // Use intervalSensorPulseCount for interval pulse counts.
  
    checkForFlow();
  
    readAdcs();

    computeCurrentFlowRate();

    computeConsumption();

    debugPrintFlowDetails();
  
    printFlowDetails();
}

// Check if their is flow on any sensor.
// Needs more than ... pulses per the interval to count as as flowing
// otherwise ignored as noise.
// TODO: with low pulse count, check for possible leak.
// If flowing, the "indicatorOnCount" is set to the number of intervals
// that are used to indicate a flow session.
void checkForFlow() {

  // Check sensors to see if any has flow.
  for (int i=0; i<SENSOR_COUNT; i++) {
    // if more than n pulses, then it is flowing.
    // otherwise it's just noise.
    
    // this is pulses per second, so a very low flow rate may be below this.
    if (intervalSensorPulseCount[i] > 2) {
      // Number of intervals to allow before 
      // flow rate is reset.
      indicatorOnCount = 4;
      hasFlow = true;
    }
  }

  // Check for stopped flow...
  // Per interval, reduce the flowing indicator.
  indicatorOnCount--;

  if (indicatorOnCount >=0 ) {
    hasFlow = true;
  } else {
    indicatorOnCount = 0;
    
    if (hasFlow) {
      Serial.println("Flow has stopped...");
      updateOnStoppedFlow();
    }
    hasFlow = false;
  }

  setFlowIndicator();
}


// Check if flow has stopped and update session variables...
void updateOnStoppedFlow() {
  // Was flowing, but now not, so store the previous sessions flow consumption.
  for (int i=0; i<SENSOR_COUNT; i++) {   
    lastConsumedLitres[i] = currentConsumedLitres[i];
    currentConsumedLitres[i] = 0;
  }

  printSessionUsage();
}

// Read the ADCs for input voltage and leak sensors.
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

// Compute the flow rate this interval.
void computeCurrentFlowRate() {
  for (int i=0; i<SENSOR_COUNT; i++) {
    if (intervalSensorPulseCount[i] == 0) {
      currentFlowRate[i] = 0;
    } else {
      // computer how many litres have gone through the sensor.
      float intervalVolume = intervalSensorPulseCount[i] / flowPulsesPerLitre[i];
     
      // Multiply by a factor to get it into litres per minute.
      currentFlowRate[i] = intervalVolume * (60000 / UPDATE_INTERVAL);
    }
  }
}

// Compute the consumption, per session (from start of flow to end of flow
void computeConsumption() {
  for (int i=0; i<SENSOR_COUNT; i++) {
    if (intervalSensorPulseCount[i] > 0) {
      float intervalVolume = intervalSensorPulseCount[i] / flowPulsesPerLitre[i];
      currentConsumedLitres[i] += intervalVolume;
      totalConsumedLitres[i] += intervalVolume;
      todayTotalConsumedLitres[i] += intervalVolume;
    }
  }
}

void updateDailyStats() {
  for (int i=0; i<SENSOR_COUNT; i++) {
    yesterdaysTotalConsumedLitres[i] = todayTotalConsumedLitres[i];
    todayTotalConsumedLitres[i] = 0;
  }

  printDailyStats();
}

// Print flow details through debug (USB Serial) port.
void debugPrintFlowDetails() {
  if (!hasFlow) {
    return;
  }
  
  // Total pulses...
  Serial.print("Total Pulses. Cold: ");
  Serial.print(totalSensorPulseCount[0], DEC);
  Serial.print(", Hot: ");
  Serial.print(totalSensorPulseCount[1], DEC);

  // Interval pulses.
  Serial.print("Interval Pulses. Cold: ");
  Serial.print(intervalSensorPulseCount[0], DEC);
  Serial.print(", Hot: ");
  Serial.print(intervalSensorPulseCount[1], DEC);

  // Flow rate.
  Serial.print(", Flow Cold: ");
  Serial.print(currentFlowRate[0], 2);
  Serial.print("L/Min, Hot: ");
  Serial.print(currentFlowRate[1], 2);
  Serial.print("L/Min");

  // Volume consumed. This ...
  Serial.print(", Volume Cold: ");
  Serial.print(currentConsumedLitres[0], 2);
  Serial.print("L, Hot: ");
  Serial.print(currentConsumedLitres[1], 2);
  Serial.print("L");

/*
  Serial.print(", Last Cold: ");
  Serial.print(lastConsumedLitres[0], 2);
  Serial.print("L, Hot: ");
  Serial.print(lastConsumedLitres[1], 2);
  Serial.print("L");

  Serial.print(", Total Cold: ");
  Serial.print(totalConsumedLitres[0], 1);
  Serial.print("L, Hot: ");
  Serial.print(totalConsumedLitres[1], 1);
  Serial.print("L");
  */
  
  // Input voltage
  //Serial.print(", inputVoltage: " ); 
  //Serial.print(inputVoltage);

  // Leak sensors.
  /*
  Serial.print(", leakSenseFault: " ); 
  Serial.print(leakSenseFault);
  Serial.print(", leakSense2: " ); 
  Serial.print(leakSense2);
  Serial.print(", leakSense1: " ); 
  Serial.print(leakSense1);
  */

  // Valves
  //Serial.print(", hotValveEnabled: " ); 
  //Serial.print(hotValveEnabled);
  //Serial.print(", coldValveEnabled: " ); 
  //Serial.print(coldValveEnabled);

  // Sensor power.
  //Serial.print(", sensorPowerEnabled: " ); 
  //Serial.print(sensorPowerEnabled);

  Serial.println();
}

// Print flow details through Serial port (connected to WIZNet ethernet connector).
void printFlowDetails() {
    if (!hasFlow) {
    return;
  }

  // Total pulses...
  Serial1.print("Total Pulses. Cold: ");
  Serial1.print(totalSensorPulseCount[0], DEC);
  Serial1.print(", Hot: ");
  Serial1.print(totalSensorPulseCount[1], DEC);

  // Interval pulses.
  Serial1.print("Interval Pulses. Cold: ");
  Serial1.print(intervalSensorPulseCount[0], DEC);
  Serial1.print(", Hot: ");
  Serial1.print(intervalSensorPulseCount[1], DEC);

  // Flow rate.
  Serial1.print(", Flow Cold: ");
  Serial1.print(currentFlowRate[0], 2);
  Serial1.print("L/Min, Hot: ");
  Serial1.print(currentFlowRate[1], 2);
  Serial1.print("L/Min");

  // Volume consumed. This ...
  Serial1.print(", Volume Cold: ");
  Serial1.print(currentConsumedLitres[0], 2);
  Serial1.print("L, Hot: ");
  Serial1.print(currentConsumedLitres[1], 2);
  Serial1.print("L");
  
  Serial1.print(", Last Cold: ");
  Serial1.print(lastConsumedLitres[0], 2);
  Serial1.print("L, Hot: ");
  Serial1.print(lastConsumedLitres[1], 2);
  Serial1.print("L");

  Serial1.print(", Total Cold: ");
  Serial1.print(totalConsumedLitres[0], 2);
  Serial1.print("L, Hot: ");
  Serial1.print(totalConsumedLitres[1], 2);
  Serial1.print("L");
  
  // Input voltage
  Serial1.print(", inputVoltage: " ); 
  Serial1.print(inputVoltage);

  Serial1.println();
}

// Print the session water usage
void printSessionUsage() {
  Serial1.println("------------------------------");
  Serial1.println("Session: ");
  for (int i=0; i<SENSOR_COUNT; i++) {
    printSessionWaterUsageSensorSummary(i);
  }
  Serial1.println("------------------------------");
}

// Prints the water usage summary (once per minute).
void printWaterUsageSummary() {

  Serial1.println("------------------------------");
  
  if (hasFlow) {
    Serial1.println("Water is currently being consumed.");  

    Serial1.println("Current Usage: ");
    for (int i=0; i<SENSOR_COUNT; i++) {
      printCurrentWaterUsageSensorSummary(i);
    }
  }

  // Last Session
  Serial1.println("Last Session: ");
  for (int i=0; i<SENSOR_COUNT; i++) {
    printSessionWaterUsageSensorSummary(i);
  }

  // Todays usave
  Serial1.println("Today: ");
  for (int i=0; i<SENSOR_COUNT; i++) {
    printTodaysWaterUsageSensorSummary(i);
  }

  // Yesterdays usage
  Serial1.println("Yesterday: ");
  for (int i=0; i<SENSOR_COUNT; i++) {
    printYesterdaysWaterUsageSensorSummary(i);
  }

  // Total
  Serial1.println("Total Water Usage: ");
  for (int i=0; i<SENSOR_COUNT; i++) {
    printTotalWaterUsageSensorSummary(i);
  }
  Serial1.println("------------------------------");
}

void printCurrentWaterUsageSensorSummary(int sensorId) {
  Serial1.print(" " + sensorNames[sensorId] + ": ");
  Serial1.print(currentConsumedLitres[sensorId], 2);
  Serial1.println("L");
}

void printSessionWaterUsageSensorSummary(int sensorId) {
  Serial1.print(" " + sensorNames[sensorId] + ": ");
  Serial1.print(lastConsumedLitres[sensorId], 2);
  Serial1.println("L");
}

void printTotalWaterUsageSensorSummary(int sensorId) {
  Serial1.print(" " + sensorNames[sensorId] + ": ");
  Serial1.print(totalConsumedLitres[sensorId], 2);
  Serial1.println("L");
}

void printTodaysWaterUsageSensorSummary(int sensorId) {
  Serial1.print(" " + sensorNames[sensorId] + ": ");
  Serial1.print(todayTotalConsumedLitres[sensorId], 2);
  Serial1.println("L");
}

void printYesterdaysWaterUsageSensorSummary(int sensorId) {
  Serial1.print(" " + sensorNames[sensorId] + ": ");
  Serial1.print(yesterdaysTotalConsumedLitres[sensorId], 2);
  Serial1.println("L");
}

void printDailyStats() {
  float totalConsumption = 0;
  Serial1.println("Water Usage Yesterday: ");
  for (int i=0; i<SENSOR_COUNT; i++) {
    printYesterdaysWaterUsageSensorSummary(i);
    totalConsumption += yesterdaysTotalConsumedLitres[i];
  }  

  Serial1.print("Total Volume Consumed: ");
  Serial1.print(totalConsumption, 3);
  Serial1.println("L");
  
  Serial1.println("------------------------------");
}

void setFlowIndicator() {  
  digitalWrite(LED_BUILTIN, hasFlow);
}

// Check for a command from the serial port.
// Very basic, single character only.
void checkSerialCommands() {
  // WIZNet ethernet connection
  while (Serial1.available()) {
    char command = Serial1.read();
    processSerialCommand(command);
  }

  // USB serial
  while (Serial.available()) {
    char command = Serial.read();
    processSerialCommand(command);
  }
}

void processSerialCommand(char command) {
  Serial.print("Processing command: '");
  Serial.print(command);
  Serial.println("' ");
  
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
      case 'R':
        resetTotals();
        break;
      case 'D':
        // Reset the daily milliseconds to force a daily reset
        // i.e. do this at the start of the day.
        nextDayAt = 0;
        break;
      // Stats reports...
      case '1':
        printSessionUsage();
        break;
      case '2':
        printWaterUsageSummary();
        break;
      case '3':
        printDailyStats();
        break;
    }
}

void resetTotals() {
  for (int i=0; i<SENSOR_COUNT; i++) {
      totalConsumedLitres[i] = 0;
      todayTotalConsumedLitres[i];
      yesterdaysTotalConsumedLitres[i];
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
  // very VERY basic debounce...
  if (!digitalRead(6)) {
    sensorPulseCount[0]++;
    totalSensorPulseCount[0]++;
  }
}
  
void hotPulseIsr() {
  if (!digitalRead(9)) {
    sensorPulseCount[1]++;
    totalSensorPulseCount[1]++;
  }
}

