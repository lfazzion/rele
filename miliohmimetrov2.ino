//Documentação da biblioteca ADS1X15: https://github.com/RobTillaart/ADS1X15

#include "ADS1X15.h"
#include <Wire.h>

#define LED_CONT 4
#define RELAY 2
#define BUTTON 15

#define MEAN 20
#define res_ref 99.781

ADS1115 ADS(0x48);

float res[2][8] = {0.0};
float res_cal[8] = {0.0};
int value = 0;
int num = 0;
bool relay_state = 0;

void setup() {
  Serial.begin(9600);  // Initialize serial communication with 9600 baud rate
  Wire.begin();        // Initialize I2C communication

  pinMode(LED_CONT, OUTPUT);
  pinMode(RELAY, OUTPUT);
  pinMode(BUTTON, INPUT);

  digitalWrite(RELAY, 0);
  ADS.begin();
  ADS.setGain(0);
  ADS.setDataRate(4);    //  0 = slow   4 = medium   7 = fast
  ADS.setMode(1);
}

float get_res(){
  if(ADS.isConnected()){
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

void action_relay(){
  digitalWrite(RELAY, !relay_state);
  relay_state = !relay_state;
}

void calibrate(int n){
  for(int j = 0; j<n; j++){
    wait_confirmation(j);
    res_cal[j] = 0.0;
    for(int i = 0; i<MEAN; i++){
      res_cal[j] += get_res();
    }
    res_cal[j] = res_cal[j]/MEAN;
  }
  Serial.println("Calibração finalizada.");
}

void wait_confirmation(int j){
  Serial.println("Preparado para a próxima leitura do terminal" + String(j) +". Entre com '1' para continuar.");
  digitalWrite(LED_CONT, 1);
  // while(digitalRead(BUTTON)==0){}
  while(Serial.available()==0){}
  Serial.read();
  digitalWrite(LED_CONT, 0);
}

void read_voltage(int n){
  for(int k=0; k<2; k++){
    for(int j=0; j<n; j++){
      wait_confirmation(j);
      res[k][j] = 0.0;
      for(int i = 0; i<MEAN; i++){
        res[k][j] += get_res() - res_cal[j];
      }
      res[k][j] = res[k][j]/MEAN;
    }
    action_relay();
  }
}

void loop() {
  if(Serial.available()>0){
    char input = Serial.read();
    switch(input){
      case 'C':
        Serial.println("Entre com a quantidade de terminais");
        while(Serial.available()==0){}
        num = Serial.parseInt();
        calibrate(num);
        break;
      case 'V':
        Serial.println("Entre com a quantidade de terminais");
        while(Serial.available()==0){}
        num = Serial.parseInt();
        read_voltage(num);
        for(int k=0; k<2; k++){
          for(int i=0; i<num; i++){
            Serial.println("Resistencia " + String(i) + ":\t" + String(res[k][i], 3));
          }
        }
        break;
      default:
        Serial.println("Opção inválida");
        break;
    }
  }
}
