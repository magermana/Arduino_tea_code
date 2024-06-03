

//------------------------------------


//-----------------------------------------------------------------
#include <WiFiNINA.h>
#include "secrets.h"

    char ssid[] = SECRET_SSID;                // your network SSID (name)
    char pass[] = SECRET_PASS;                // your network password (use for WPA, or use as key for WEP)
    WiFiClient client;

void wifi_setup()
{
  Serial.print("Attempting to connect to WPA SSID: ");
  Serial.println(ssid);
  while (WiFi.begin(ssid, pass) != WL_CONNECTED) {
    // failed, retry
    Serial.print(".");
    delay(5000);
  }

  Serial.println("You're connected to the network");
  Serial.println();
}
//-----------------------------------------------
long starting_weight; //we want to know the starting weight (which should be zero, but things happen...)
//we will terminate the program at any point if the load sensor detects a weight lower than the starting weight
//a situation like this could mean the cup has fallen off the weight sensor. 
#include "HX711.h"
HX711 scale;
    const int weight_clock = 6;
    const int weight_data = 7;
void HX711_setup()   //HX711 Load sensor setup
{
  //start getting data from the load sensor. scale.tare() to reset to 0 to account for the weight of the cup
  scale.begin(weight_data, weight_clock);
  if (scale.is_ready())
  {
    scale.tare();
    Serial.println("scale should be good to go");
    starting_weight = scale.get_value();
  }
  else //the program will abort if the connection to the scale isn't properly established.
  {
    Serial.println("scale apparently not ready...");
    cleanup();
    exit(0);
  }
}


long getWeight() //assigns the detected weight to weight_value. Ends the program if the scale can't be found or if our current weight goes below the starting weight.
//sidenote: we would expect the starting weight to be 0, but the scale isn't perfect. 
{
  long weight;
  if (scale.is_ready()) {
    scale.set_scale();    
    Serial.println("WEIGHING...");
    delay(5000);
    weight = scale.get_value();
    if (weight < starting_weight)
    {
      Serial.println("weight lower than starting weight... something's wrong");
      cleanup();
      exit(0);
    }
    Serial.print("Result: ");
    Serial.println(weight);
    } 
  else {
    Serial.println("HX711 not found. ABORT.");
    cleanup();
    exit(0);
  }
  return weight;
}
//--------------------------------------------

#include "DHT.h"


//set up the DHT temp sensor
#define DHTPIN 4

#define DHTTYPE DHT11   
DHT dht(DHTPIN, DHTTYPE);
//---------------------


//set up wifi connection

//-------------------

//MQTT publish/subscribe protocol 
#include <ArduinoMqttClient.h>

    MqttClient mqttClient(client); //allows us to interact with the mqtt broker

    const char broker[] = "broker.emqx.io"; //broker host name
    int        port     = 1883;
    const char topic[]  = "SIT210/wave";

  void MQTT_setup()
  {
    Serial.print("Attempting to connect to the MQTT broker: ");
  Serial.println(broker);



  if (!mqttClient.connect(broker, port)) {
    Serial.print("MQTT connection failed! Error code = ");
    Serial.println(mqttClient.connectError());

    while (1);
  }

  Serial.println("You're connected to the MQTT broker!");
  mqttClient.onMessage(onMqttMessage);

  mqttClient.subscribe(topic);

  Serial.println();

  }
//----------------------------------------

//defining pins
    const int water_pin = 1;
    const int heating_pin = 2;
    const int Dispencer_pin = 3; //IMPORTANT: pins 1, 2 and 3 power their components with an NPN transistor; HIGH = OFF and LOW = ON
    const int smoke_pin = A0;

    //data pin for temp sensor = 4
//------------------------------------------
  const int max_allowable_smoke = 1;
  int Smoke_Check()
  {
    Serial.println("SMOKE CHECK:");
    int smoke_value = analogRead(A0);
    if(smoke_value > max_allowable_smoke)
    {
      cleanup();
      Serial.println("excess smoke detected. ending program...");
      exit(0);
    }
    return smoke_value;
  }
  const float max_allowable_heat = 99;
  float Heat_Check()
  {
    Serial.println("HEAT CHECK");
    float tempValue = dht.readTemperature();
    if (tempValue > max_allowable_heat)
    {
      cleanup();
      Serial.println("excess heat! aborting program.");
      exit(0);
    }
    return tempValue;
  }



void cleanup()
{
  digitalWrite(water_pin, HIGH);
  digitalWrite(heating_pin, HIGH);
  digitalWrite(Dispencer_pin, HIGH);

}
/*
void Exit_Routine(int error_code)
{
  Serial.println("Exit triggered.")
  switch(error_code)
  {case 1: }
}
*/

void Dispence_teabags(int teaBagQuantity = 1)
{
  //make sure a valid number of tea bags has been chosen.
  if (teaBagQuantity > 2)
  {
    Serial.println("maximum two teabags");
    return;
  }
  else if(teaBagQuantity < 1)
  {
    Serial.println("you need at least 1 tea bag!");
  }
  //dispence the tea bags. Here it takes about one second to dispence one tea bag.
  digitalWrite(Dispencer_pin, LOW);
  delay((500 * teaBagQuantity));
  digitalWrite(Dispencer_pin, HIGH);

}
const long min_weight = 20;
const long max_weight = 30;
void Fill_Cup(int teaBagQuantity = 1)
{
  unsigned long start_time = millis();
 
  //getting the water into the cup
  unsigned long current_time;
  while((current_time - start_time) > 1200) //time limit of 10 seconds
  {

    digitalWrite(water_pin, LOW);
    long weight_value = getWeight();
    if(weight_value >= min_weight)
    {
      digitalWrite(water_pin, HIGH);
      break;
    }
    current_time = millis();

  }
  digitalWrite(water_pin, HIGH);
  }

const int boil_time = 300; //seconds
const long allowable_weight_difference = 5; //number of grams worth of water we are willing to lose during the boiling process.
void Boil_Tea()
{
  long weight_value = getWeight();
  if (!((weight_value >= min_weight) && (weight_value <= max_weight)))
  {
    Serial.println("the Fill_Cup routine has failed. Can't continue");
    cleanup();
    exit(0);
  } //if we detect too much or too little weight, there is not enough water, or some other malfunction.

  unsigned long start_time = millis();
  unsigned long current_time;
  unsigned long boil_time = 300000; //boil for 5 minutes
  long start_weight = getWeight(); //we take note of the start weight. if this sees a significant change then the cup may have fallen or something like that. We should stop immediatley.
  while((current_time - start_time) < boil_time*1000)
  {
    digitalWrite(heating_pin, LOW);
    int smoke_value = Smoke_Check();
    float temp = Heat_Check();
    weight_value = getWeight();

    //the cup is going to lose a small amount of weight since the water in evaporating.
    //but if this amount is excessive, it could indicate that the cup has fallen, or maybe we have boiled too much.
    if((weight_value - starting_weight) > allowable_weight_difference)
    {
      digitalWrite(heating_pin, HIGH);
      cleanup();
      Serial.println("too much weight lost. must stop boiling");
      exit(0);
    }
    Smoke_Check();
    Heat_Check();
    current_time = millis();
  }
}

void setup() {
  pinMode(water_pin, OUTPUT);
  pinMode(heating_pin, OUTPUT);
  pinMode(Dispencer_pin, OUTPUT);
  pinMode(smoke_pin, INPUT);

  digitalWrite(water_pin, HIGH);
  digitalWrite(heating_pin, HIGH);
  digitalWrite(Dispencer_pin, HIGH); //High is the default state for these pins since we're using NPN transistors.

  Serial.begin(9600);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }

  HX711_setup();
  dht.begin();
//
  // attempt to connect to Wifi network:
  wifi_setup();

  MQTT_setup();
  //the maximum time the pump is allowed to be on for, in seconds.
 //determines the minimum allowable water quantity for boiling to begin.

  
}

void loop()
{
  
  long weight_value = getWeight();
  delay(1000);
  weight_value = getWeight();
  int smokeValue = Smoke_Check();
  float tempValue = dht.readTemperature();
  mqttClient.poll();
  


  
}

void onMqttMessage(int messageSize) {
  // we received a message, print out the topic and contents
  Serial.println("Received a message with topic '");
  Serial.print(mqttClient.messageTopic());
  Serial.print("', length ");
  Serial.print(messageSize);
  Serial.println(" bytes:");

  //so trigger the
  char first_char = (char)mqttClient.read();
  if(first_char == 'x')
  {
    Serial.println("message recieved! time for tea");
    //begin making tea
    Dispence_teabags(1);
    Fill_Cup();
    long weight_value = getWeight();
  if ((weight_value >= min_weight) && (weight_value <= max_weight))
  {
    Boil_Tea();
  }
  else
  {

    exit(0);
  }
  }
  
  Serial.println();
  Serial.println();
}



