/*
 * Documentação da biblioteca ADS1X15: https://github.com/RobTillaart/ADS1X15
 * 
 */

#include "ADS1X15.h"
#include <Wire.h>

#define A 15
#define B 2
#define C 4
#define RELAY 5
#define INH 18

#define MEAN 20
#define res_ref 99.781

ADS1115 ADS(0x48);

static char binaryString[3];
float res[2][8] = {0.0};
float r0[8] = {0.0};
int value = 0;
int num = 0;
bool relay_state = 0;

void setup() {
  Serial.begin(9600);  // Initialize serial communication with 9600 baud rate
  Wire.begin();        // Initialize I2C communication

  pinMode(A, OUTPUT);
  pinMode(B, OUTPUT);
  pinMode(C, OUTPUT);
  pinMode(RELAY, OUTPUT);
  pinMode(INH, OUTPUT);

  digitalWrite(INH, HIGH);
  digitalWrite(RELAY, 0);
  ADS.begin();
  ADS.setGain(0);
}

float get_res(){
  int16_t val_01 = ADS.readADC_Differential_0_1();
  int16_t val_13 = ADS.readADC_Differential_1_3();
  float volts_ref = ADS.toVoltage(val_01);
  float volts = ADS.toVoltage(val_13);  

  return (res_ref*volts)/volts_ref;
}

void action_relay(){
  digitalWrite(RELAY, !relay_state);
  relay_state = !relay_state;
}

void calibrate(int n){
  digitalWrite(INH, LOW);
  for(int j = 0; j<n; j++){
    float r0_base = 0.0;
    for(int i = 0; i<MEAN; i++){
      r0_base += get_res();
    }
    r0[j] = r0_base/MEAN;
  }
  digitalWrite(INH, HIGH);
  Serial.print("Calibração finalizada.");
}

void read_voltage(int n){
  digitalWrite(INH, LOW);
  for(int k=0; k<2; k++){
    for(int j=0; j<n; j++){
      multiplx_controler(j);
      delay(500);
      res[k][j] = 0.0;
      for(int i = 0; i<MEAN; i++){
        res[k][j] += get_res() - r0[j];
      }
      res[k][j] = res[k][j]/MEAN;
    }
    action_relay();
  }
  digitalWrite(INH, HIGH);
}

void toBinary(int num) {
  int pos = 0;
  for (int i = 2; i >= 0; i--) { // Assuming 3-bit integer
    if ((num >> i) & 1) {
      binaryString[pos] = '1';
    } else {
      binaryString[pos] = '0';
    }
    pos++;
  }
}

void multiplx_controler(int OT){
  toBinary(OT);
  digitalWrite(A, (binaryString[0] == '1') ? HIGH : LOW);
  digitalWrite(B, (binaryString[1] == '1') ? HIGH : LOW);
  digitalWrite(C, (binaryString[2] == '1') ? HIGH : LOW);
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
