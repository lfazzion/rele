/* Referências:
  Documentação da biblioteca ADS1X15: https://github.com/RobTillaart/ADS1X15
  Manipulação String -> Array: https://arduino.stackexchange.com/questions/77125/convert-string-to-array
*/

#include "ADS1X15.h"
#include <Wire.h>
#include <BluetoothSerial.h>

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif

#define LED_CONT  4
#define LED_ON    25
#define RGB_RED   14
#define RGB_GREEN 27
#define RGB_BLUE  26
#define RELAY_DC  2
#define RELAY_AC  12
#define BUTTON    15

#define MEAN      20
#define res_ref   99.781

ADS1115 ADS(0x48);
BluetoothSerial SerialBT;

float res[2][8] = {0.0};
float res_cal = 0.0;
int value = 0;
int num = 0;
bool relay_state = 0;

void setup() {
  Serial.begin(9600);             // Inicializa a comunicação Serial com 9600 baud rate
  SerialBT.begin("Jiga Rele BT"); // Inicializa a cominucação Serial Bluetooth com o nome "Jiga Rele BT"
  Wire.begin();                   // Inicializa a comunicação I2C

  pinMode(LED_CONT, OUTPUT);
  pinMode(LED_ON, OUTPUT);
  pinMode(RGB_RED, OUTPUT);
  pinMode(RGB_GREEN, OUTPUT);
  pinMode(RGB_BLUE, OUTPUT);
  pinMode(RELAY_AC, OUTPUT);
  pinMode(RELAY_DC, OUTPUT);
  
  pinMode(BUTTON, INPUT);

  digitalWrite(RELAY_DC, 0);
  digitalWrite(RELAY_AC, 0);
  ADS.begin();
  ADS.setGain(1);
}

String menssure_time(int relay_type){
  int16_t val = ADS.readADC_Differential_0_3();
  unsigned long startTime = micros();
  while(val == ADS.readADC_Differential_0_3()){digitalWrite(relay_type, 1);}
  unsigned long endTime = micros();
  unsigned long duration_on = endTime - startTime;

  unsigned long start = milis();
  while(milis()-start<1000){}

  val = ADS.readADC_Differential_0_3();
  startTime = micros();
  while(val == ADS.readADC_Differential_0_3()){digitalWrite(relay_type, 0);}
  endTime = micros();
  unsigned long duration_off = endTime - startTime;

  return "On:"+String(duration_on)+";Off:"+String(duration_off);
}

void reset_output(){
  digitalWrite(RGB_RED, 0);
  digitalWrite(RGB_GREEN, 0);
  digitalWrite(RGB_BLUE, 0);
  digitalWrite(RELAY_DC, 0);
  digitalWrite(RELAY_AC, 0);
}

void state_RGB(char state){
  switch(state){
    case "O":
      digitalWrite(RGB_RED, 0);
      digitalWrite(RGB_GREEN, 0);
      digitalWrite(RGB_BLUE, 1);
      break;
    case "B":
      digitalWrite(RGB_RED, 0);
      digitalWrite(RGB_GREEN, 1);
      digitalWrite(RGB_BLUE, 0);
      break;
    case "R":
      digitalWrite(RGB_RED, 1);
      digitalWrite(RGB_GREEN, 0);
      digitalWrite(RGB_BLUE, 0);
      break;
  }
}

float get_res(){
  if(ADS.isConnected()){
    ADS.setDataRate(0);    //  0 = slow   4 = medium   7 = fast

    int16_t val_01 = ADS.readADC_Differential_0_1();
    int16_t val_13 = ADS.readADC_Differential_1_3();
    float volts_ref = ADS.toVoltage(val_01);
    float volts = ADS.toVoltage(val_13);

    return (res_ref*volts)/volts_ref;
  }else{
    Serial.println("ADS não encontrado"); 
    return 0.0;
  }
}

void action_relay(int relay_action){
  relay_state = !relay_state;
  digitalWrite(relay_action, !relay_state);
}

void calibrate(){
  wait_confirmation();
  res_cal = 0.0;
  for(int i = 0; i<MEAN; i++){
    res_cal += get_res();
  }
  res_cal = res_cal/MEAN;
}

void wait_confirmation(int j = 0){
  if(j==0){
    SerialBT.println("Preparado para calibração. Pressione o botão para iniciar.");
  }else{
    SerialBT.println("Preparado para a próxima leitura do terminal" + String(j) +". Entre com '1' para continuar.");
  }
  digitalWrite(LED_CONT, 1);
  while(digitalRead(BUTTON)==0){}
  digitalWrite(LED_CONT, 0);
}

void read_voltage(int n, int relay_action){
  for(int k=0; k<2; k++){
    for(int j=0; j<n; j++){
      wait_confirmation(j+1);
      res[k][j] = 0.0;
      for(int i = 0; i<MEAN; i++){
        res[k][j] += get_res() - res_cal;
      }
      res[k][j] = res[k][j]/MEAN;
    }
    action_relay(relay_action);
  }
}

void loop() {
  if(SerialBT.available()>0){
    String input = SerialBT.String();
    switch(input[0]){
      case 'C':
        calibrate();
        SerialBT.println("Calibração realizada");
        break;
      case 'V':
        
        break;
      default:
        SerialBT.println("0");
        break;
    }
  }
}
