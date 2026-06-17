#include <TM1637TinyDisplay.h>
#include <ezBuzzer.h>
//Pins------------
//fuel rods
#define FUEL1_POT A1
#define FUEL2_POT A0

//control rod
#define CONTROL_ROD_POT1 A3
#define CONTROL_ROD_POT2 A2
#define A35 2

//Pumps
#define PUMP1_POT A5
#define PUMP2_POT A4
#define TURPIN 11

//display
#define DISPLAY_CLOCK 3
#define DISPLAY_REACTIVITY 7
#define DISPLAY_POWER 6
#define DISPLAY_HEAT 5
#define DISPLAY_WATER 4

//warnings
#define WARNING_TEMP 8
#define WARNING_WATER 13
#define WARNING_REACTIVITY 9
#define WARNING_CRIT 12
#define WARNING_PIZO 10

//relay
#define RELAY 1
//end pins ---------


//TÖÖPÕHIMÕTE:
//Reaktiivsus soendab reaktori tuuma, mis soendab omakorda vett ta ümber. Kui vesi jõuab üle 70c, hakkab ta aurustuma
//veeauru tekkimine jahutab vett, vähendab veetaset ning seda saab kasutada energia tootmiseks.
//PUMBAD: Lisavad vett ning jahutavad veetemperatuuri.


//vesi
const double STARTING_WATER = 2000.0;

//reaction parameters
const double FUEL_REACTIVITY = 0.004;  //Kui reaktiivne kütus on
const double CONTROL_ROD_EFFECT = 0.32; //Kui palju kontrollvardad reaktiivsust pärsivad
const double HEAT_RATE = 0.004; //SOOJA GENEREERIMIS EFFEKT radioaktiivsuse poolt, ehk palju tuum soojeneb
const double COOLING_EFFECT = .1; //how chill steam is
const double PUMP_EFFECT = 0.18; //PUMBA EFFEKTIIVSUS
const double PUMP_COOLING_EFFECT = 0.10; //Kui palju pump vett jahutab
const double HEAT_RAD_EFFECT = 0.0003; //kui palju kõrge(vee) temp lisab radiaktiivsusele juurde
const double TRANSFER_RATE = 0.025; //kui hästi tuum veele sooja annab

const double MAX_REACTIVITY = 7000.0; //REAKTIIVSUSE HOIATUS
const double MAX_HEAT = 95.0; //KUUMAHOIATUS
const double MIN_WATERLEVEL = 1500.0; //VEETASEME HOIATUS
const double STARTUP_REACTIVITY = 5.0; //Reaktor üritab end startida selle väärtusega
const double STEAM_EFFECT = 1.4; //kui palju auru tekib vees üle 70c
const double PUMP_POWER_DRAW = 2.0; //palju pumbad voolu võtavad ((pump1 + pump 2) / sellega) (pumbad on 0-100)

//VARS-------------
//reactor
double reactivity = 0.0;
double water_temp = 11.0; //startup heat
double core_temp = 11.0; //startup core temp
double power = 0.0;
int control_rod_depth = 0;
int pump1_speed = 0;
int pump2_speed = 0;
bool turbine_enabled = 0;
double water_level = STARTING_WATER;
int fuel_depth = 0;
int fuel_tune = 0;


//clock
unsigned long lastUpdateTime = 0;
const unsigned long TICKRATE = 500; //Kui tihti seade end updateb.

//hoiatused
bool warning_water = 0;
bool warning_temp = 0;
bool warning_reactivity = 0;
bool warning_crit = 0;

//initialize displays
TM1637TinyDisplay TempDisplay = TM1637TinyDisplay(DISPLAY_CLOCK, DISPLAY_HEAT);
TM1637TinyDisplay ReactivityDisplay = TM1637TinyDisplay(DISPLAY_CLOCK, DISPLAY_REACTIVITY);
TM1637TinyDisplay PowerDisplay = TM1637TinyDisplay(DISPLAY_CLOCK, DISPLAY_POWER);
TM1637TinyDisplay WaterLevelDisplay = TM1637TinyDisplay(DISPLAY_CLOCK, DISPLAY_WATER);


//and the buzza
ezBuzzer alarm(WARNING_PIZO);

//Toore inputi printout:
bool raw_print = 0; //muuta 1ks kui vaja näha raw printe serialis.

void setup() {
    //initialize serial
    Serial.begin(9600);

    pinMode(TURPIN, INPUT_PULLUP); // pin low = inserted true
    pinMode(A35, INPUT_PULLUP);

    pinMode(WARNING_TEMP, OUTPUT);
    pinMode(WARNING_WATER, OUTPUT);
    pinMode(WARNING_REACTIVITY, OUTPUT);
    pinMode(WARNING_CRIT, OUTPUT);

    TempDisplay.begin();
    ReactivityDisplay.begin();
    PowerDisplay.begin();
    WaterLevelDisplay.begin();

    TempDisplay.flipDisplay(true);
    ReactivityDisplay.flipDisplay(true);
    PowerDisplay.flipDisplay(true);
    WaterLevelDisplay.flipDisplay(true);


    //finish setup message

    Serial.println("Reactor online, weapons online, all systems nominal");
}//end setup



void loop() {

    alarm.loop();

    unsigned long currentTime = millis();
    if(currentTime-lastUpdateTime >= TICKRATE){ //checks if its time to update
        readInputs();
        simulateReactor();
        simulateTurbine();
        printStatus();
        displayInfo();
        alarmSound();
        toggleRelay();

        //restart funktsioon:
        if (core_temp > 900 || water_temp > 210){
            alarm.beep(1000);
            delay(1000);
            TempDisplay.showString("----");
            ReactivityDisplay.showString("----");
            WaterLevelDisplay.showString("----");
            PowerDisplay.showString("----");
            delay(1000);
            TempDisplay.showString("REST");
            ReactivityDisplay.showString("ART");
            for(int i = 5; i != 0; i--){
                PowerDisplay.showNumber(i);
                delay(1000);
            }
            NVIC_SystemReset();

        }
        //set current time
        lastUpdateTime = currentTime;
    }
}//end loop

void readInputs(){

    //kui emergency brake on peal siis kõik asjad nulli, meibi lisada ka natuke buusti
    if (digitalRead(A35) == LOW){
        PowerDisplay.showString("A35");

        fuel_depth = 0;
        fuel_tune = 0;

        control_rod_depth = 1500;

        turbine_enabled = 0;

        pump1_speed = 110;
        pump2_speed = 110;

        alarm.beep(200);

    } else {
        turbine_enabled = (digitalRead(TURPIN) == LOW);

        int control1_raw = analogRead(CONTROL_ROD_POT1);
        int control2_raw = analogRead(CONTROL_ROD_POT2);
        int pump1_raw = analogRead(PUMP1_POT);
        int pump2_raw = analogRead(PUMP2_POT);


        int fuel1_raw = analogRead(FUEL1_POT);
        int fuel_tune_raw = analogRead(FUEL2_POT);

        //parandab tagurpidi fuel rawi ära, kui kütuse main on välja lülitatud..
        if(fuel1_raw < 10){
            fuel1_raw = 1023;
        }

        fuel_depth = map(fuel1_raw, 0, 1023, 1023, 0);
        fuel_tune = map(fuel_tune_raw, 0, 1023, 0, 256);

        //Raw printout
        if (raw_print = 1){
            Serial.print("Pump1 / 2 raw: ");
            Serial.print(pump1_raw);
            Serial.print(" / ");
            Serial.print(pump2_raw);
            Serial.print(" | Control 1/2 raw:");
            Serial.print(control1_raw);
            Serial.print(" / ");
            Serial.print(control2_raw);
            Serial.print(" | Fuel 1/tune raw:");
            Serial.print(fuel1_raw);
            Serial.print(" / ");
            Serial.print(fuel_tune_raw);
        }



        int control_rod1_depth = map(control1_raw, 0, 1024, 0, 512);
        int control_rod2_depth = map(control2_raw, 0, 1024, 0, 1024);

        control_rod_depth = control_rod1_depth + control_rod2_depth;

        pump1_speed = map(pump1_raw, 0, 1024, 0, 100);
        pump2_speed = map(pump2_raw, 0, 4095, 0, 100);
    }

}//end readinputs


void simulateReactor(){
    //delta time calculation
    double dt = (double)TICKRATE / 1000.0; //DT delta time
    double dR_dt; //delta taim reaktiivsus ajas


    // --- Reactivity calculation ---
    double rods_reactivity = ((fuel_depth) * FUEL_REACTIVITY) + (fuel_tune * FUEL_REACTIVITY);
    double control_effect = CONTROL_ROD_EFFECT * (double)control_rod_depth;
    double self_sustain = reactivity * (HEAT_RAD_EFFECT * water_temp); //hoiab reaktiivsust ka siis kui kütust pole
    // kickstart
    if (reactivity == 0.0 && fuel_depth > 100) {
        reactivity = STARTUP_REACTIVITY;
    }

    // reactivity growth when all things applied
    dR_dt = (rods_reactivity * sqrt(reactivity + 1.0)) + self_sustain - control_effect;


    reactivity += dR_dt * dt;

    //clamp reactivity to pos.
    if (reactivity < 0.0) reactivity = 0.0;
    //---------------heat simulation-------------

    double dCore_dt =
    (HEAT_RATE * reactivity)
    - (core_temp - water_temp) * 0.05;

    core_temp += dCore_dt * dt;

    // Water heats slowly
    double dWater_dt =
    (core_temp - water_temp) * TRANSFER_RATE
    - ((pump1_speed + pump2_speed) * PUMP_COOLING_EFFECT) / 4.0;

    water_temp += dWater_dt * dt;
    if(water_temp< 1.0) water_temp = 1.0;

}//end simulateReactor

//todo: effektiivsuspiirkond lisada turbiinile sõltuvalt reaktori iseloomust,
void simulateTurbine(){
    double dt = (double)TICKRATE / 1000.0;


    //kui vesi soe tee auru ja vähenda vett
    if (water_temp > 70.0){
        double steam = (water_temp - 70) * STEAM_EFFECT;

        //kui vett on üle miinimumi ära fläshboili seda
        if (water_level > 1000.0){
            if (water_level > STARTING_WATER){
                water_level = STARTING_WATER;
            }

            water_temp = water_temp - COOLING_EFFECT * steam;
            if(water_temp > 100.0){
                water_temp = 100.0;
            }
        }

        double steam_cooling_effect = steam * COOLING_EFFECT;


        //veekadu on aur korda aeg
        double evap_rate = (steam * dt);

        //vesi kaub evap ratega ja tuleb juurde pumbaga
        water_level = water_level - evap_rate + ((pump1_speed + pump2_speed) * PUMP_EFFECT);;

        //clamp water level to min max
        if(water_level < 0.0){
            water_level = 0.0;
        }

        if(turbine_enabled){
            evap_rate = evap_rate * 5;
            power = steam * steam - ((pump1_speed + pump2_speed)/ PUMP_POWER_DRAW);
        } else {
            power = power - (3 * dt);
        }
    }
}//end turbine

void displayInfo(){

    int tempInfo = static_cast<int>(water_temp);
    int reactivityInfo = static_cast<int>(reactivity);
    int powerInfo = static_cast<int>(power);
    int waterLevel = static_cast<int>(water_level);

    tempInfo = constrain(tempInfo, 1, 9999);
    reactivityInfo = constrain(reactivityInfo, 0, 9999);
    powerInfo = constrain(powerInfo, 0, 9999);
    waterLevel = constrain(waterLevel, 0, 9999);

    TempDisplay.showString("\xB0", 1, 3);        // Degree Mark, length=1, position=3 (right)
    TempDisplay.showNumber(tempInfo, false, 3, 0);    // Number, length=3, position=0 (left)

    ReactivityDisplay.showNumber(reactivityInfo);
    PowerDisplay.showNumber(powerInfo);
    WaterLevelDisplay.showNumber(waterLevel);
}

void printStatus() {
    char buffer[128];

    // Header
    Serial.println("+--------------------------------------------------------------------------------+");
    Serial.print("  fuel_depth");
    Serial.print(fuel_depth);
    Serial.print("  control_rod_depth");
    Serial.print(control_rod_depth);
    Serial.print("  pumps");
    Serial.print( pump1_speed);
    Serial.print(", ");
    Serial.print( pump2_speed);
    Serial.println();
    Serial.print("  reactivity");
    Serial.print( reactivity);
    Serial.print("  temp W");
    Serial.print( water_temp);
    Serial.print("  Core Temp");
    Serial.print( core_temp);
    Serial.print("  power");
    Serial.print( power);
    Serial.print("  turbine");
    Serial.print( turbine_enabled);
    Serial.print("  water level");
    Serial.print(water_level);
    Serial.println();
    Serial.println("+--------------------------------------------------------------------------------+");

    // Warnings

    ///PIMEDAS HELENDAMISE HOIATUS ////////////////////
    if (reactivity > MAX_REACTIVITY) {
        warning_reactivity = 1;
        Serial.println(" WARNING: Reactivity runaway imminent!");
    } else{
        warning_reactivity = 0;
    }

    ///HEA LEILI HOIATUS ////////////////////
    if (water_temp > MAX_HEAT || water_temp < 10.0) {
        warning_temp = 1;
        Serial.println(" Water temp warning!");
    } else {
        warning_temp = 0;
    }

    /// HALVA UJUMISE HOIATUS ////////////////////
    if (water_level < MIN_WATERLEVEL) {
        Serial.println("ALERT: Water level low!");
        warning_water = 1;
    } else {
        warning_water = 0;
    }

    ///NO IKKA TÄISHOIATUS ////////////////////
    if (core_temp > 900){
        warning_crit = 1;
    } else {
        warning_crit = 0;
    }

    digitalWrite(WARNING_REACTIVITY, warning_reactivity ? HIGH : LOW);
    digitalWrite(WARNING_TEMP, warning_temp ? HIGH : LOW);
    digitalWrite(WARNING_WATER, warning_water ? HIGH : LOW);
    digitalWrite(WARNING_CRIT, warning_crit ? HIGH : LOW);
}//endprintstatus

void alarmSound(){
    bool alarmActive = 0;
    bool anyWarning =
    warning_water ||
    warning_temp ||
    warning_reactivity ||
    warning_crit;

    if (anyWarning && !alarmActive) {
        alarm.beep(15000);
        alarmActive = true;
    }

    if (!anyWarning && alarmActive) {
        alarm.stop();
        alarmActive = false;
    }
}

void toggleRelay(){
    static unsigned long cycleStart = 0;
    unsigned long now = millis();

    // Start a new 4-second cycle
    if (now - cycleStart >= 4000) {
        cycleStart = now;
    }
    unsigned long delta = now - cycleStart;
    if(power > 2000){
        digitalWrite(RELAY,HIGH);
        Serial.println("RELAY ON!");

    }else if(power > 1500){
        if(delta < 3000){
            digitalWrite(RELAY,HIGH);
            Serial.println("RELAY ON!");
        }else{
            digitalWrite(RELAY, LOW);
        }

    }else if(power > 1000){
        if(delta < 2000){
            digitalWrite(RELAY,HIGH);
            Serial.println("RELAY ON!");
        }else{
            digitalWrite(RELAY, LOW);
        }

    }else if(power > 500){
        if(delta < 1000){
            digitalWrite(RELAY,HIGH);
            Serial.println("RELAY ON!");
        }else{
            digitalWrite(RELAY, LOW);
        }
    } else{
        digitalWrite(RELAY, LOW);
    }
}
