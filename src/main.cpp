#include <Arduino.h>
#include <uButton.h>
#include <freqmeter.h>
#include <GTimer.h>
#define HallD 2
#define OUTPUT_PWM 3
#define OUTPUT_LEFT 4 // выгрузка
#define OUTPUT_RIGHT 5 // загрузка
#define IndicatorLed 6
uButton BUTTON_INC(7);
uButton BUTTON_DEC(8);
uButton BUTTON_STOP(9);

FreqMeter freq;

GTimer<millis> tmr1(65,true); // Таймер для обработки логики изменения ШИМ при удержании кнопок 
GTimer<millis> tmr2(50,true); // Таймер для обновления состояния оборотов двигателя машины и телеметрии
GTimer<millis> tmr3(40, true); // Таймер для компенсации ШИМ
GTimer<millis> blinkTimer(500, true); // Таймер для мигания индикатора при ошибке

bool ledState = false;
bool alert_hall = false;

uint32_t current_rpm = 0; // Текущие обороты, измеренные с помощью датчика Холла
uint16_t base_rpm = 600; // Базовые обороты для расчёта компенсации

uint16_t button_press_duration = 200;             // Длительность удержания для первого срабатывания, мс

uint8_t result_pwm = 0;

uint8_t right_flag = 0; 
uint8_t left_flag = 0;

uint8_t pwm_min = 0;
uint8_t pwm_max = 255;
uint8_t pwm_step = 1;
uint8_t pwm_zero = 0;
uint8_t pwm_setpoint = 0;
uint8_t pwm_first_step = 38;

uint8_t pwm_min_for_compensation = 54;
uint8_t comensPWM = 0;

float ratio_comens(uint32_t x){
  uint16_t rpm_min = 600;
  uint16_t rpm_max = 2500;
  uint32_t safe_x = constrain(x,rpm_min, rpm_max);
  float convert_rpm_to_ratio = 1.0 - ((float)(safe_x - rpm_min)/(rpm_max - rpm_min))* (1.0 - 0.1);
  return convert_rpm_to_ratio;
}
void setup() {
  Serial.begin(115200);
  attachInterrupt(digitalPinToInterrupt(HallD), []() { freq.pinFront(); }, FALLING);
  pinMode(OUTPUT_PWM, OUTPUT);
  pinMode(OUTPUT_LEFT, OUTPUT);
  pinMode(OUTPUT_RIGHT, OUTPUT);
  pinMode(IndicatorLed, OUTPUT);

  
  
  digitalWrite(OUTPUT_LEFT, LOW);
  digitalWrite(OUTPUT_RIGHT, LOW);

  digitalWrite(IndicatorLed, HIGH);
  delay(1000);
  digitalWrite(IndicatorLed, LOW);
}
// Функция для проверки состояния датчика Холла и управления индикатором
void checkHall() {
  // Фиксируем ошибку, если мотор запущен, но обороты упали
  if (current_rpm < 200 && pwm_setpoint > 0) {
    if (alert_hall == false) {
      alert_hall = true;
      // Ставим целевые значения в 0 - это вызовет плавное снижение comensPWM в таймере tmr3
      result_pwm = 0;
      pwm_setpoint = pwm_zero;
    }
  } 

  // Если флаг ошибки взведен, постоянно мигаем
  if (alert_hall) {
    if (blinkTimer.tick()) {
      ledState = !ledState;
      digitalWrite(IndicatorLed, ledState); // Инвертируем состояние для мигания
    }
  } else {
    // Штатный режим работы
    digitalWrite(IndicatorLed, LOW);
    ledState = false;
  }
}

void loop() {
  BUTTON_INC.tick();
  BUTTON_DEC.tick();
  BUTTON_STOP.tick();
  checkHall();
  // Логика изменения ШИМ 
  if (tmr1.tick()) { 
    
    // Обработка кнопки INC
    if (BUTTON_INC.pressFor(button_press_duration)) {
      if (pwm_setpoint == pwm_zero && left_flag == 0 ) { // Проверяем, что не крутимся влево
        digitalWrite(OUTPUT_LEFT, LOW);
        digitalWrite(OUTPUT_RIGHT, HIGH);
        right_flag = 1;
        left_flag = 0;
        pwm_setpoint = pwm_first_step;
        Serial.println("OUTPUT_RIGHT enabled");
      } else {
        if (left_flag == 1) { // Тормозим левое вращение
          // Безопасное вычитание
          if (pwm_setpoint >= pwm_step) pwm_setpoint -= pwm_step; 
          else pwm_setpoint = 0; 
          
          if (pwm_setpoint == pwm_zero) { 
            left_flag = 0;
            digitalWrite(OUTPUT_LEFT, LOW);
            BUTTON_INC.reset(); 
          }
        } else if (right_flag == 1 && pwm_setpoint <= pwm_max - pwm_step) {
          pwm_setpoint += pwm_step; // Разгоняем вправо
        }
      }
    }

    // Обработка кнопки DEC
    if (BUTTON_DEC.pressFor(button_press_duration)) {
      if (pwm_setpoint == pwm_zero && right_flag == 0) {
        digitalWrite(OUTPUT_LEFT, HIGH);
        digitalWrite(OUTPUT_RIGHT, LOW);
        right_flag = 0;
        left_flag = 1;
        pwm_setpoint = pwm_first_step;
        Serial.println("OUTPUT_LEFT enabled");
      } else {
        if (right_flag == 1) { // Тормозим правое вращение
          if (pwm_setpoint >= pwm_step) pwm_setpoint -= pwm_step;
          else pwm_setpoint = 0;
          
          if (pwm_setpoint == pwm_zero) { 
            right_flag = 0;
            digitalWrite(OUTPUT_RIGHT, LOW);
            BUTTON_DEC.reset();
          }
        } else if (left_flag == 1 && pwm_setpoint <= pwm_max - pwm_step) {
          pwm_setpoint += pwm_step; // Разгоняем влево
        }
      }
    }
  } // Конец блока таймера tmr1

  if (BUTTON_STOP.release()) {
    pwm_setpoint = pwm_zero;
    result_pwm = 0;
    comensPWM = 0; // Сбрасываем текущий ШИМ для мгновенной остановки
    digitalWrite(OUTPUT_LEFT, LOW);
    digitalWrite(OUTPUT_RIGHT, LOW);
    analogWrite(OUTPUT_PWM, 0);
    left_flag = 0;  
    right_flag = 0;
    Serial.println("STOP");
  }
  // Логика компенсации ШИМ для поддержания стабильных оборотов
  if (tmr3.tick()) {
    if (comensPWM < result_pwm) {
      comensPWM += pwm_step;
      if (comensPWM > result_pwm) comensPWM = result_pwm;
    } else if (comensPWM > result_pwm) {
      comensPWM -= pwm_step;
      if (comensPWM < result_pwm) comensPWM = result_pwm;
    }
  }
  

  // Обновление состояния моторов и телеметрии 
  if (tmr2.tick()) {
    current_rpm = freq.getRPMmercedes_with_hall();
    
    if (pwm_setpoint == pwm_zero) {
      result_pwm = 0; 
      // Выключаем реле направлений ТОЛЬКО когда компенсационный ШИМ плавно дошел до 0
      if (comensPWM == 0) {
        digitalWrite(OUTPUT_LEFT, LOW);
        digitalWrite(OUTPUT_RIGHT, LOW);
        analogWrite(OUTPUT_PWM, 0);
        left_flag = 0;  
        right_flag = 0;
      }
    }
    else {
      // Если НЕТ ошибки и ШИМ <= 128 - работает компенсация
      if (!alert_hall && pwm_setpoint <= 128 && !left_flag) {
        if (current_rpm > 200) {
          float calc_pwm = pwm_setpoint * ratio_comens(current_rpm) ;
          if (calc_pwm < pwm_min_for_compensation) result_pwm = pwm_min_for_compensation;
          else if (calc_pwm > pwm_max) result_pwm = pwm_max;
          else result_pwm = (uint8_t)calc_pwm;
        } else {
          result_pwm = pwm_setpoint;
        }
        
        // Ограничения для режима С КОМПЕНСАЦИЕЙ
        if (result_pwm < pwm_min_for_compensation && pwm_setpoint > 0) result_pwm = pwm_min_for_compensation; 
        if (result_pwm > pwm_max) result_pwm = pwm_max; 

      }
      else if(left_flag == 1){
        result_pwm = pwm_setpoint;
      }
      else {
        // РЕЖИМ БЕЗ КОМПЕНСАЦИИ (после ошибки или если ШИМ > 128)
        // Напрямую транслируем заданный ШИМ без ограничений снизу
        result_pwm = pwm_setpoint;
      }
    }
    
    Serial.print(" Setpoint (PWM): ");
    Serial.println(pwm_setpoint);
    Serial.print(" Result PWM: ");
    Serial.println(result_pwm);
    Serial.print(" compensated PWM: ");
    Serial.println(comensPWM);
    Serial.print(" RPM: ");
    Serial.println(current_rpm);
    analogWrite(OUTPUT_PWM, constrain(comensPWM, 0, 255));
  }
}
