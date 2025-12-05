#include "stm32h750xx.h"
#include "stm32h7xx_hal.h"
#include "stm32h7xx_hal_pwr_ex.h"
// For the USB connection detection
inline void PA2_EXTI_Init(void) {
    // Jaffx::Firmware::instance->hardware.SetLed(true);
    /* ---------------------- Enable Clocks ---------------------- */
    
    /* GPIOA clock */
    RCC->AHB4ENR |= RCC_AHB4ENR_GPIOAEN;
    
    /* ---------------------- Configure PA2 as Input ---------------------- */
    /* MODER2 = 00 (input) */
    GPIOA->MODER &= ~GPIO_MODER_MODE2;
    
    /* Use internal pulldown */
    GPIOA->PUPDR |= GPIO_PUPDR_PUPD2_1;
    
    /* SYSCFG clock */
    RCC->APB4ENR |= RCC_APB4ENR_SYSCFGEN;
    
    /* ---------------------- Connect PA2 to EXTI2 ---------------------- */
    SYSCFG->EXTICR[0] &= ~SYSCFG_EXTICR1_EXTI2; // Clear EXTI2 bits
    // SYSCFG->EXTICR[0] |= SYSCFG_EXTICR1_EXTI2_PA; // Set EXTI2 to Port A (default)
    
    /* Rising trigger enabled */
    EXTI->RTSR1 |= EXTI_RTSR1_TR2;
    
    /* Falling trigger enabled */
    EXTI->FTSR1 |= EXTI_FTSR1_TR2;
    
    /* Unmask interrupt */
    EXTI->IMR1 |= EXTI_IMR1_IM2;
    
    // SlipRecorder::hardware.PrintLine()
    /* ---------------------- NVIC Configuration ---------------------- */
    
    NVIC_EnableIRQ(EXTI2_IRQn);
    NVIC_SetPriority(EXTI2_IRQn, 1);
}

// Gonna want to be able to access whether USB is connected in sleep mode, no need for deinitialization

// For the USB connection debounce
inline void TIM13_Init(void) {
    /* Enable TIM13 clock */
    RCC->APB1LENR |= RCC_APB1LENR_TIM13EN;

    TIM13->CR1 &= ~TIM_CR1_CKD; // (set to same as internal clock = no clock division)
    TIM13->CNT = 0; // Reset counter
    
    

    uint32_t apb1ClkFreq = HAL_RCC_GetPCLK1Freq(); // We find it's 100MHz

    // TODO: Find a dynamic way to set PSC and ARR for any APB1 clock frequency
    //  to achieve a ~x ms wait time (such that PSC and ARR fit in 16-bits)


    // Jaffx::Firmware::instance->hardware.PrintLine("apb1ClkFreq: %lu", apb1ClkFreq);
    // // Calculate prescaler for 1kHz timer clock
    // uint32_t prescaler = (apb1ClkFreq / 1000) - 1;
    // // Safety check: PSC must fit in 16-bits
    // if (prescaler > 0xFFFF) {
    //     Jaffx::Firmware::instance->hardware.PrintLine("Prescaler overflow: %lu", prescaler);
    //     // prescaler = 0xFFFF;  // Clamp to max if calculation overflows
    // }
    
    // Use apb1ClkFreq and prescaler to set timer frequency to 1kHz
    TIM13->PSC = apb1ClkFreq / 1000000 - 1; // Prescaler value for 1us timer clock
    // Set the auto-reload interval for desired debounce time of ~50ms here
    TIM13->ARR = 50000-1;   // Auto-reload value for 50ms    
    
    /* Enable update interrupt */
    TIM13->DIER |= TIM_DIER_UIE;
    
    /* Enable TIM13 interrupt in NVIC */
    NVIC_EnableIRQ(TIM8_UP_TIM13_IRQn);
    NVIC_SetPriority(TIM8_UP_TIM13_IRQn, 2);
}

inline void EnableUSBDetect(void) {
    PA2_EXTI_Init();
    TIM13_Init();
}


// For the SD Card connection detection
inline void PB12_EXTI_Init(void) {
//   Jaffx::Firmware::instance->hardware.SetLed(true);
  /* ---------------------- Enable Clocks ---------------------- */

  /* GPIOB clock */
  RCC->AHB4ENR |= RCC_AHB4ENR_GPIOBEN;

  /* ---------------------- Configure PB12 as Input ---------------------- */
  /* MODER12 = 00 (input) */
  GPIOB->MODER &= ~GPIO_MODER_MODE12;

  /* No pull-up / pull-down */
  GPIOB->PUPDR &= ~GPIO_PUPDR_PUPD12;

  /* SYSCFG clock */
  RCC->APB4ENR |= RCC_APB4ENR_SYSCFGEN;

  /* ---------------------- Connect PB12 to EXTI12 ---------------------- */
  SYSCFG->EXTICR[3] &= ~SYSCFG_EXTICR4_EXTI12; // Clear EXTI12 bits
  SYSCFG->EXTICR[3] |= SYSCFG_EXTICR4_EXTI12_PB; // Set EXTI12 to Port B

  /* Rising trigger enabled */
  EXTI->RTSR1 |= EXTI_RTSR1_TR12;

  /* Falling trigger enabled */
  EXTI->FTSR1 |= EXTI_FTSR1_TR12;

  /* Unmask interrupt */
  EXTI->IMR1 |= EXTI_IMR1_IM12;

  // SlipRecorder::hardware.PrintLine()
  /* ---------------------- NVIC Configuration ---------------------- */
  
  NVIC_EnableIRQ(EXTI15_10_IRQn);
  NVIC_SetPriority(EXTI15_10_IRQn, 1);
}

// For the SD Card connection debounce
inline void TIM14_Init(void) {
    /* Enable TIM14 clock */
    RCC->APB1LENR |= RCC_APB1LENR_TIM14EN;

    TIM14->CR1 &= ~TIM_CR1_CKD; // (set to same as internal clock = no clock division)
    TIM14->CNT = 0; // Reset counter

    uint32_t apb1ClkFreq = HAL_RCC_GetPCLK1Freq(); // We find it's 100MHz

    TIM14->PSC = apb1ClkFreq / 10000 - 1; // Prescaler value for 1us timer clock
    // Set the auto-reload interval for desired debounce time of ~50ms here
    TIM14->ARR = 7500;   // Auto-reload value   
    
    /* Enable update interrupt */
    TIM14->DIER |= TIM_DIER_UIE;
    
    /* Enable TIM14 interrupt in NVIC */
    NVIC_EnableIRQ(TIM8_TRG_COM_TIM14_IRQn);
    NVIC_SetPriority(TIM8_TRG_COM_TIM14_IRQn, 2);
}

inline void EnableSDCardDetect(void) {
    PB12_EXTI_Init();
    TIM14_Init();
}


inline void PB12_EXTI_DeInit(void) {
    /* Disable EXTI line 12 interrupt */
    EXTI->IMR1 &= ~EXTI_IMR1_IM12;
    NVIC_DisableIRQ(EXTI15_10_IRQn);
}

inline void TIM14_DeInit(void) {
    TIM14->CR1 &= ~TIM_CR1_CEN; // Disable TIM14
    /* Disable TIM14 update interrupt */
    NVIC_DisableIRQ(TIM8_TRG_COM_TIM14_IRQn);
    /* Disable TIM14 clock */
    RCC->APB1LENR &= ~RCC_APB1LENR_TIM14EN;
}

inline void DisableSDCardDetect(void) {
    PB12_EXTI_DeInit();
    TIM14_DeInit();
}


// For pulsing recording LED
inline void PA4_GPIO_Init(void) {
    /* ---------------------- Enable Clocks ---------------------- */

    /* GPIOA clock */
    RCC->AHB4ENR |= RCC_AHB4ENR_GPIOAEN;

    /* ---------------------- Configure PA4 as Output ---------------------- */
    /* MODER4 = 01 (output) */
    GPIOA->MODER &= ~GPIO_MODER_MODE4;
    GPIOA->MODER |= GPIO_MODER_MODE4_0;

    /* Output type push-pull */
    GPIOA->OTYPER &= ~GPIO_OTYPER_OT4;

    /* No pull-up / pull-down */
    GPIOA->PUPDR &= ~GPIO_PUPDR_PUPD4;

    GPIOA->OSPEEDR &= ~GPIO_OSPEEDR_OSPEED4; // Lowest speed
}

// For pulsing recording LED
inline void TIM16_Init(void) {
    /* Enable TIM16 clock */
    RCC->APB2ENR |= RCC_APB2ENR_TIM16EN;

    TIM16->CR1 &= ~TIM_CR1_CKD; // (set to same as internal clock = no clock division)
    TIM16->CNT = 0; // Reset counter

    uint32_t apb2ClkFreq = HAL_RCC_GetPCLK2Freq(); // We find it's 100MHz
    // Jaffx::Firmware::instance->hardware.PrintLine("apb2ClkFreq: %lu", apb2ClkFreq);

    TIM16->PSC = apb2ClkFreq / 10000 - 1; // Prescaler value for 1us timer clock
    // Set the auto-reload interval for desired debounce time of ~50ms here
    TIM16->ARR = 15000;   // Auto-reload value   
    
    /* Enable update interrupt */
    TIM16->DIER |= TIM_DIER_UIE;
    
    /* Enable TIM16 interrupt in NVIC */
    NVIC_EnableIRQ(TIM16_IRQn);
    NVIC_SetPriority(TIM16_IRQn, 2);
}

inline void EnableRecordingLED(void) {
    PA4_GPIO_Init();
    TIM16_Init();
}

inline void StartRecordingLED(void) {
    /* Enable TIM16 */
    TIM16->CNT = 0; // Reset counter
    TIM16->CR1 |= TIM_CR1_CEN;
    GPIOA->ODR |= GPIO_ODR_OD4; // Turn on LED immediately
}

inline void PA4_GPIO_Deinit(void) {
    // Deinit GPIOA pin 4
    RCC->AHB4ENR &= ~RCC_AHB4ENR_GPIOAEN; // Disable GPIOA clock
    // Set to input and pull-down to save power
    GPIOA->MODER &= ~GPIO_MODER_MODE4; // Set MODER4 to 00 (input)

    GPIOA->PUPDR &= ~GPIO_PUPDR_PUPD4; // Clear PUPDR4 bits
    GPIOA->PUPDR |= GPIO_PUPDR_PUPD4_1; // Set PUPDR4 to 10 (pull-down)
}

inline void TIM16_DeInit(void) {
    TIM16->CR1 &= ~TIM_CR1_CEN; // Disable TIM16
    /* Disable TIM16 update interrupt */
    NVIC_DisableIRQ(TIM16_IRQn);
    /* Disable TIM16 clock */
    RCC->APB2ENR &= ~RCC_APB2ENR_TIM16EN;
}

inline void StopRecordingLED(void) {
    /* Disable TIM16 */
    TIM16->CR1 &= ~TIM_CR1_CEN;
    GPIOA->ODR &= ~GPIO_ODR_OD4; // Turn off LED immediately
}

inline void DisableRecordingLED(void) {
    StopRecordingLED();
    /* Disable TIM16 interrupt */
    TIM16_DeInit();
    // Deinit GPIOA pin 4
    PA4_GPIO_Deinit();
}
// Actually handle toggling the LED
extern "C" void TIM16_IRQHandler(void) {
    if (TIM16->SR & TIM_SR_UIF) { // Check update interrupt flag
        TIM16->SR &= ~TIM_SR_UIF; // Clear update interrupt flag
        // Toggle the recording LED
        GPIOA->ODR ^= GPIO_ODR_OD4;
    }
}

// For the power button
inline void PC0_EXTI_Init(void) {
//   Jaffx::Firmware::instance->hardware.SetLed(true);
  /* ---------------------- Enable Clocks ---------------------- */

  /* GPIOC clock */
  RCC->AHB4ENR |= RCC_AHB4ENR_GPIOCEN;

  /* ---------------------- Configure PC0 as Input ---------------------- */
  /* MODER0 = 00 (input) */
  GPIOC->MODER &= ~GPIO_MODER_MODE0;

  /* No pull-up / pull-down */
  GPIOC->PUPDR &= ~GPIO_PUPDR_PUPD0;

  /* SYSCFG clock */
  RCC->APB4ENR |= RCC_APB4ENR_SYSCFGEN;

  /* ---------------------- Connect PC0 to EXTI0 ---------------------- */
  SYSCFG->EXTICR[0] &= ~SYSCFG_EXTICR1_EXTI0; // Clear EXTI0 bits
  SYSCFG->EXTICR[0] |= SYSCFG_EXTICR1_EXTI0_PC; // Set EXTI0 to Port C

  /* Rising trigger enabled */
  EXTI->RTSR1 |= EXTI_RTSR1_TR0;

  /* Falling trigger disabled */
  EXTI->FTSR1 &= ~EXTI_FTSR1_TR0;

  /* Unmask interrupt */
  EXTI->IMR1 |= EXTI_IMR1_IM0;

  // SlipRecorder::hardware.PrintLine()
  /* ---------------------- NVIC Configuration ---------------------- */
  
  NVIC_EnableIRQ(EXTI0_IRQn);
  NVIC_SetPriority(EXTI0_IRQn, 1);
}

inline void EnablePowerButtonDetect(void) {
    PC0_EXTI_Init();
}
