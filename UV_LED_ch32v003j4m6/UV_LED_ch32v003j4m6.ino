#include <Arduino.h>
#include <ch32v00x_flash.h>

// Призначення пінів CH32V003J4M6 (SOP8) згідно даташиту:
// Ніжка 1: PD6/PA1        — WS2812 (бітбанг)
// Ніжка 2: VSS (GND)
// Ніжка 3: PA2             — не використовується
// Ніжка 4: VDD
// Ніжка 5: PC1             — Кнопка яскравості
// Ніжка 6: PC2             — Кнопка таймера
// Ніжка 7: PC4 (T1CH4)     — PWM MOSFET
// Ніжка 8: PD4/PD5/PD1     — бондована з SWIO, не використовується
#define BTN_BRIGHT  PC1  // Ніжка 5
#define BTN_TIMER   PC2  // Ніжка 6
#define LED_PWM_PIN PC4  // Ніжка 7 (TIM1_CH4 PWM)
#define WS2812_PIN  PD6  // Ніжка 1

#define FLASH_SETTINGS_ADDR  0x08003FC0
#define FLASH_MAGIC          0xABCD
#define WS2812_BRIGHTNESS    60  // Максимальна яскравість світлодіода WS2812 (0..255)

// Вибір типу транзистора:
// 0 — N-Channel MOSFET (LOW = OFF, HIGH = ON) — за замовчуванням
// 1 — P-Channel MOSFET (HIGH = OFF, LOW = ON)
#define USE_P_CHANNEL_MOSFET 0



// Налаштування яскравості ліхтаря
static const uint8_t brightnessLevels[] = {0, 75, 150, 255};
int currentMode = 0;
int lastActiveMode = 3; // Запам'ятовує останній режим для увімкнення

// Налаштування таймера
static const unsigned long timerDurations[] = {10 * 1000UL, 30 * 1000UL, 60 * 1000UL};
int currentTimerMode = 0; 
bool timerEnabled = true; // Стан таймера (увімкнено/вимкнено)

void saveSettings() {
  uint16_t w0 = ((uint16_t)timerEnabled << 8) | currentTimerMode;
  uint16_t w1 = FLASH_MAGIC;

  uint16_t currentW0 = *(__IO uint16_t*)(FLASH_SETTINGS_ADDR);
  uint16_t currentW1 = *(__IO uint16_t*)(FLASH_SETTINGS_ADDR + 2);

  if (currentW0 == w0 && currentW1 == w1) {
    return;
  }

  FLASH_Unlock();
  FLASH_ClearFlag(FLASH_FLAG_BSY | FLASH_FLAG_EOP | FLASH_FLAG_WRPRTERR);
  
  FLASH_Status status = FLASH_ErasePage(FLASH_SETTINGS_ADDR);
  if (status == FLASH_COMPLETE) {
    FLASH_ProgramHalfWord(FLASH_SETTINGS_ADDR, w0);
    FLASH_ProgramHalfWord(FLASH_SETTINGS_ADDR + 2, w1);
  }
  FLASH_Lock();
}

void loadSettings() {
  uint16_t w0 = *(__IO uint16_t*)(FLASH_SETTINGS_ADDR);
  uint16_t w1 = *(__IO uint16_t*)(FLASH_SETTINGS_ADDR + 2);

  if (w1 == FLASH_MAGIC) {
    currentTimerMode = w0 & 0xFF;
    timerEnabled = (w0 >> 8) & 0xFF;

    if (currentTimerMode > 2) currentTimerMode = 0;
    if (timerEnabled > 1) timerEnabled = 1;
  }
}


unsigned long modeStartTime = 0;

// Таймінги для довгого натискання кнопок
unsigned long brightPressTime = 0;
bool brightIsPressing = false;
bool brightLongHandled = false;

unsigned long timerPressTime = 0;
bool timerIsPressing = false;
bool timerLongHandled = false;

const unsigned long debounceDelay = 50;

uint32_t lastSentColor = 0xFFFFFF; // Зберігає останній фактично відправлений колір

uint8_t getWS2812Brightness() {
  if (currentMode == 0) {
    uint8_t min_b = WS2812_BRIGHTNESS / 5;
    return (min_b < 5) ? 5 : min_b; // Режим очікування: 20% від макс, але не менше 5
  }
  // Лінійне збільшення яскравості відповідно до потужності лампи (1..3)
  return (uint16_t)WS2812_BRIGHTNESS * currentMode / 3;
}

// Низькорівневе керування WS2812 на порту PD6 (реальні 24 МГц)
// Біт "1": HIGH ~800нс, LOW ~450нс
// Біт "0": HIGH ~400нс, LOW ~850нс
void sendPixelColor(uint8_t r, uint8_t g, uint8_t b) {
  uint8_t brightness = getWS2812Brightness();
  // Масштабуємо яскравість кожного каналу
  r = ((uint16_t)r * brightness) >> 8;
  g = ((uint16_t)g * brightness) >> 8;
  b = ((uint16_t)b * brightness) >> 8;

  uint32_t colorData = ((uint32_t)g << 16) | ((uint32_t)r << 8) | b;
  
  if (colorData == lastSentColor) {
    return; // Колір не змінився, виходимо
  }
  lastSentColor = colorData;

  noInterrupts();
  for (uint32_t mask = 0x800000; mask > 0; mask >>= 1) {
    if (colorData & mask) {
      GPIOD->BSHR = (1 << 6); // HIGH
      __asm__ volatile("nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;");
      GPIOD->BSHR = (1 << (6 + 16)); // LOW
      __asm__ volatile("nop;nop;nop;nop;nop;");
    } else {
      GPIOD->BSHR = (1 << 6); // HIGH
      __asm__ volatile("nop;nop;nop;");
      GPIOD->BSHR = (1 << (6 + 16)); // LOW
      __asm__ volatile("nop;nop;nop;nop;nop;nop;nop;nop;");
    }
  }
  interrupts();
  delayMicroseconds(300);
}

// Оновлення кольору індикатора WS2812
void updateIndicators() {
  // Перевірка миготіння в останні 5 секунд таймера
  if (currentMode > 0 && timerEnabled) {
    unsigned long elapsed = millis() - modeStartTime;
    unsigned long duration = timerDurations[currentTimerMode];
    if (elapsed < duration) {
      unsigned long remaining = duration - elapsed;
      if (remaining <= 5000) {
        bool blinkOn = (remaining / 500) % 2;
        if (!blinkOn) {
          sendPixelColor(0, 0, 0); // Вимикаємо світлодіод у фазі паузи миготіння
          return;
        }
      }
    }
  }

  if (!timerEnabled) {
    sendPixelColor(255, 100, 0); // Таймер вимкнено — Жовтий (постійне світло)
  } else {
    if (currentTimerMode == 0)      sendPixelColor(0, 255, 0);     // 10 сек - Зелений
    else if (currentTimerMode == 1) sendPixelColor(0, 200, 255);   // 30 сек - Бірюзовий
    else if (currentTimerMode == 2) sendPixelColor(255, 0, 255);   // 60  сек - Фіолетовий
  }
}

// Ручна ініціалізація TIM1_CH4 PWM на PC4 (ніжка 7)
void pwmInit() {
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC | RCC_APB2Periph_TIM1 | RCC_APB2Periph_AFIO, ENABLE);

  GPIO_InitTypeDef gpioInit = {0};
  gpioInit.GPIO_Pin = GPIO_Pin_4;
  gpioInit.GPIO_Mode = GPIO_Mode_AF_PP;
  gpioInit.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_Init(GPIOC, &gpioInit);

  TIM_TimeBaseInitTypeDef timBase = {0};
  timBase.TIM_Period = 255;
  timBase.TIM_Prescaler = (SystemCoreClock / 256000) - 1;
  timBase.TIM_ClockDivision = TIM_CKD_DIV1;
  timBase.TIM_CounterMode = TIM_CounterMode_Up;
  timBase.TIM_RepetitionCounter = 0;
  TIM_TimeBaseInit(TIM1, &timBase);

  TIM_OCInitTypeDef ocInit = {0};
  ocInit.TIM_OCMode = TIM_OCMode_PWM1;
  ocInit.TIM_OutputState = TIM_OutputState_Enable;
  ocInit.TIM_Pulse = 0;
#if USE_P_CHANNEL_MOSFET
  ocInit.TIM_OCPolarity = TIM_OCPolarity_Low;  // Активний низький рівень (LOW = ON) для P-Channel
#else
  ocInit.TIM_OCPolarity = TIM_OCPolarity_High; // Активний високий рівень (HIGH = ON) для N-Channel
#endif
  TIM_OC4Init(TIM1, &ocInit);

  TIM_OC4PreloadConfig(TIM1, TIM_OCPreload_Enable);
  TIM_ARRPreloadConfig(TIM1, ENABLE);
  TIM_CtrlPWMOutputs(TIM1, ENABLE);
  TIM_Cmd(TIM1, ENABLE);
}

// Встановити duty cycle 0..255
void pwmSetDuty(uint8_t duty) {
  if (duty == 0) {
    // Повністю вимикаємо ШІМ та жорстко притискаємо пін
    GPIO_InitTypeDef gpioInit = {0};
    gpioInit.GPIO_Pin = GPIO_Pin_4;
    gpioInit.GPIO_Mode = GPIO_Mode_Out_PP;
    gpioInit.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOC, &gpioInit);
#if USE_P_CHANNEL_MOSFET
    GPIO_SetBits(GPIOC, GPIO_Pin_4);   // HIGH = OFF для P-Channel
#else
    GPIO_ResetBits(GPIOC, GPIO_Pin_4); // LOW = OFF для N-Channel
#endif
  } else {
    // Вмикаємо режим альтернативної функції (ШІМ)
    GPIO_InitTypeDef gpioInit = {0};
    gpioInit.GPIO_Pin = GPIO_Pin_4;
    gpioInit.GPIO_Mode = GPIO_Mode_AF_PP;
    gpioInit.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOC, &gpioInit);
    TIM_SetCompare4(TIM1, duty);
  }
}

void setup() {
  pinMode(BTN_BRIGHT, INPUT_PULLUP);
  pinMode(BTN_TIMER, INPUT_PULLUP);
  pinMode(WS2812_PIN, OUTPUT);

  loadSettings();
  currentMode = 0; // Завжди вимкнено при увімкненні живлення

  pwmInit();
  pwmSetDuty(brightnessLevels[currentMode]);
  updateIndicators();
}

void loop() {
  unsigned long now = millis();
  int readBright = digitalRead(BTN_BRIGHT);
  int readTimer = digitalRead(BTN_TIMER);

  // --- ОБРОБКА КНОПКИ ЯСКРАВОСТІ (ВМК/ВИМК ТА РЕЖИМИ) ---
  if (readBright == LOW && !brightIsPressing) {
    brightIsPressing = true;
    brightPressTime = now;
    brightLongHandled = false;
  }
  if (brightIsPressing && !brightLongHandled && (now - brightPressTime > 1000)) {
    brightLongHandled = true; // Довге натискання
    if (currentMode > 0) {
      currentMode = 0; // Вимикаємо ліхтар
    } else {
      currentMode = 1; // Вмикаємо на мінімальну потужність
      modeStartTime = now;
    }
    pwmSetDuty(brightnessLevels[currentMode]);
    updateIndicators();
  }
  if (readBright == HIGH && brightIsPressing) {
    brightIsPressing = false;
    if (!brightLongHandled && (now - brightPressTime > debounceDelay)) {
      if (currentMode == 0) {
        currentMode = 1; // Вмикаємо на мінімальну потужність
      } else {
        currentMode++; // Коротке натискання
        if (currentMode > 3) currentMode = 0;
      }
      if (currentMode > 0) lastActiveMode = currentMode;
      
      pwmSetDuty(brightnessLevels[currentMode]);
      updateIndicators();
      if (currentMode > 0) modeStartTime = now;
    }
  }

  // --- ОБРОБКА КНОПКИ ТАЙМЕРА (ВМК/ВИМК ТА ТАЙМЕРА ТА ЧАС) ---
  if (readTimer == LOW && !timerIsPressing) {
    timerIsPressing = true;
    timerPressTime = now;
    timerLongHandled = false;
  }
  if (timerIsPressing && !timerLongHandled && (now - timerPressTime > 1000)) {
    timerLongHandled = true; // Довге натискання
    timerEnabled = !timerEnabled;
    updateIndicators();
    saveSettings(); // Зберігаємо стан таймера
    if (timerEnabled) modeStartTime = now; 
  }
  if (readTimer == HIGH && timerIsPressing) {
    timerIsPressing = false;
    if (!timerLongHandled && (now - timerPressTime > debounceDelay)) {
      currentTimerMode++; // Коротке натискання
      if (currentTimerMode > 2) currentTimerMode = 0;
      timerEnabled = true;
      
      updateIndicators();
      saveSettings(); // Зберігаємо режим таймера
      if (currentMode > 0) modeStartTime = now; 
    }
  }

  // --- РОБОТА ТАЙМЕРА АВТОВИМКНЕННЯ ---
  if (currentMode > 0 && timerEnabled) {
    if (now - modeStartTime >= timerDurations[currentTimerMode]) {
      currentMode = 0;
      pwmSetDuty(brightnessLevels[currentMode]);
    }
  }

  updateIndicators();
}