/*
 * Documentação da biblioteca ADS1X15: https://github.com/RobTillaart/ADS1X15
 * 
 */

#include "ADS1X15.h"
#include <Wire.h>

#define A 15
#define B 2
#define C 4

ADS1115 ADS(0x48);

const float res_ref = 99.781;
static char binaryString[3];
float res;
float r0[8] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
int value = 0;

void setup() {
  Serial.begin(9600);  // Initialize serial communication with 9600 baud rate
  Wire.begin();        // Initialize I2C communication

  pinMode(A, OUTPUT);
  pinMode(B, OUTPUT);
  pinMode(C, OUTPUT);

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

void calibrate(){
  for(int j = 0; j<8; j++){
    float r0_base = 0.0;
    for(int i = 0; i<10; i++){
      r0_base += get_res();
    }
    r0[j] = r0_base/10;
  }
  Serial.print("Calibração finalizada.");
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
        calibrate();
        break;
      case 'V':
        for(int j=0; j<6; j++){
          multiplx_controler(j);
          delay(500);
          res = 0;
          for(int i = 0; i<20; i++){
            res += get_res() - r0[j];
          }
          res = res/20;
  
          Serial.print("Resistencia ");
          Serial.print(j);
          Serial.print(":\t");
          Serial.println(res, 3);
        }
        break;
      default:
        Serial.println("Opção inválida");
    }
  }
}
