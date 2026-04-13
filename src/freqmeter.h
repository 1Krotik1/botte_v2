#include <Arduino.h>

class FreqMeter {
   public:
    static constexpr uint32_t minPrd = 5000;
    static constexpr uint32_t maxPrd = 20000;
    static constexpr uint8_t minReads2 = 0;
    static constexpr uint8_t maxReads2 = 7;

    // вызывать в прерывании по FALLING/RISING
    void pinFront() {
        if (--_count) return;                   // ждём накопления периодов

        if (_tPrev) {                           // измерение уже идёт
            uint32_t us = micros() - _tPrev;    // время с прошлого измерения
            _tPrev += us;                       // равносильно _tPrev = micros()
            _prd = (_prd + (us >> _reads2)) >> 1;   // делим общее время на кол-во периодов + ср. арифм. с прошлым
            _ready = true;                      // флаг готовности

            // подбор кол-ва периодов
            if (us < minPrd && _reads2 < maxReads2) ++_reads2;
            else if (us > maxPrd && _reads2 > minReads2) --_reads2;
        } else {
            _tPrev = micros();
        }
        _count = (1 << _reads2);    // к-во периодов хранится как степень двойки
    }

    // сброс измерения
    void reset() {
        noInterrupts();
        _count = (1 << _reads2);
        _tPrev = micros();
        _ready = false;
        interrupts();
    }

    // период измерен, само сбросится в false
    bool ready() {
        return _ready ? _ready = false, true : false;
    }

    operator bool() {
        return ready();
    }

    // получить период в мкс
    uint32_t getPeriod() {
        noInterrupts();
        uint32_t prd = _prd;
        interrupts();
        return prd;
    }

    // получить частоту в Гц
    uint32_t getFreq() {
        uint32_t prd = getPeriod();
        return prd ? 1000000ul / prd : 0;
    }

    // получить частоту в Гц
    float getFreqFloat() {
        uint32_t prd = getPeriod();
        return prd ? 1000000.0f / prd : 0;
    }

    // получить частоту в об/мин
    uint32_t getRPM() {
        return getFreq() * 60ul;
    }

    uint32_t getRPMmercedes_with_hall() {
        return getFreq() * 8 / 10ul;
    }

    // прошло с последнего фронта, мкс
    uint32_t lastChange() {
        noInterrupts();
        uint32_t us = _tPrev;
        interrupts();
        return micros() - us;
    }

   private:
    volatile uint32_t _tPrev = 0;
    volatile uint32_t _prd = 0;
    volatile uint8_t _count = 1;
    volatile uint8_t _reads2 = 0;
    volatile bool _ready = false;
};