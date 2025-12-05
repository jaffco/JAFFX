#include "../../Jaffx.hpp"
#include "StateMachine.hpp"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include "stm32h750xx.h"
#include "stm32h7xx_hal.h"
#include "stm32h7xx_hal_pwr_ex.h"


// TODO: Move these to the dedicated DMA section exposed in daisy_core.h
// Global SD resources (hardware-required placement in AXI SRAM for DMA)
SdmmcHandler global_sdmmc_handler __attribute__((section(".sram1_bss")));
FatFSInterface global_fsi_handler __attribute__((section(".sram1_bss")));
FIL global_wav_file __attribute__((section(".sram1_bss")));
#include "SDCard.hpp"


// TODO: Every interrupt + timer init needs a corresponding deinit function 

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

void sleepMode();

class SlipRecorder : public Jaffx::Firmware {
private:
  SlipRecorder() = default;
  ~SlipRecorder() = default;
  
public:
  static SlipRecorder& Instance() {
    static SlipRecorder instance;
    return instance;
  }
  SlipRecorder(const SlipRecorder&) = delete;
  SlipRecorder(SlipRecorder&&) = delete;
  SlipRecorder& operator=(const SlipRecorder&) = delete;
  SlipRecorder& operator=(SlipRecorder&&) = delete;

  GPIO mLeds[3];
  GPIO powerLED;
  float RmsReport = 0.f;
  SDCardWavWriter<> mWavWriter;  // Use default template parameter (4096 bytes)
  bool usb_connected = false;
  StateMachine mStateMachine;

  void deinit() {
    for (auto& led : mLeds) {
      led.DeInit();
    }
    mWavWriter.StopRecording();
    hardware.StopAudio();
    powerLED.Write(false); // Indicate power off
    powerLED.DeInit();
    DisableRecordingLED();
    DisableSDCardDetect();
    // No need to disable the USB or the Power Button detection as they are needed in sleep
    hardware.DeInit();
  }

  void init() override {
    debug = true;
    // Initialize LEDs
    mLeds[0].Init(seed::D21, GPIO::Mode::OUTPUT);
    mLeds[1].Init(seed::D20, GPIO::Mode::OUTPUT);
    mLeds[2].Init(seed::D19, GPIO::Mode::OUTPUT);
    powerLED.Init(seed::D22, GPIO::Mode::OUTPUT);

    // System::Delay(2000); // Wait for 2s before going into deep sleep
    // deinit();
    // sleepMode(); // Enter sleep mode
    // return;
    // Initialize SD card
    mWavWriter.InitSDCard();

    // choose initial state based on SD status
    if (mWavWriter.sdStatus()) {
      mStateMachine.setState(SD_Check::getInstance());
    } else {
      mStateMachine.setState(Sleep::getInstance());
    }
    
    // Start recording if SD card is OK
    if(mWavWriter.sdStatus()) {
      mWavWriter.StartRecording();
    }
    // Enable detection interrupts
    EnableSDCardDetect();
    EnableUSBDetect();
    EnablePowerButtonDetect();
    EnableRecordingLED();

    powerLED.Write(true); // Indicate power on
  }

  float processAudio(float in) override {
    // Write sample to SD card if recording
    if(mWavWriter.recording()) {
      mWavWriter.WriteAudioSample(in);
    }
    
    // Calculate RMS for LED display
    static int counter = 0;
    static float RMS = 0.f;
    counter++;
    RMS += in * in;
    if (counter >= buffersize){
      RmsReport = sqrt(RMS); // update report
      counter = 0; // reset counter
      RMS = 0.f; // reset RMS
    }
    
    return in; // output throughput 
  }

  void on_PB12_rising() {
      hardware.PrintLine("Rising Edge Detected");
    }
    void on_PB12_fully_risen() {
        hardware.PrintLine("SD Card Fully Removed");
        // // Handle SD card insertion
        // mStateMachine.getCurrentState()->onSDCardInserted(&mStateMachine);
    }

  void on_PB12_falling() {
    hardware.PrintLine("Falling Edge Detected");
  }
    void on_PB12_fully_fallen() {
        hardware.PrintLine("SD Card Fully Inserted");
        // Handle SD card removal
        // mStateMachine.getCurrentState()->onSDCardRemoved(&mStateMachine);
    }

  void on_PA2_rising() {
      usb_connected = true;
      hardware.PrintLine("USB Connected");
  }
  
  void on_PA2_fully_risen() {
    hardware.PrintLine("USB Fully Connected");
  }

    void on_PA2_falling() {
        usb_connected = false;
        hardware.PrintLine("USB Disconnected");
    }

    void on_PA2_fully_fallen() {
        hardware.PrintLine("USB Fully Disconnected");
    }

    void on_PC0_rising() {
        hardware.PrintLine("Power Button Pressed");
        // mStateMachine.getCurrentState()->onPowerButtonPress(&mStateMachine);
    }

  void indicateLEDs() {
    // Convert RMS to dB - Add small offset to avoid log(0)   
    float dB = 20.0f * log10f(RmsReport + 1e-12f); 
    
    // LED logic based on dB levels (same as dBMeter example)
    if (dB >= 0.0f) {
      // >= 0dB: all three LEDs on
      mLeds[0].Write(true);
      mLeds[1].Write(true);
      mLeds[2].Write(true);
    } else if (dB >= -6.0f) {
      // >= -6dB: LEDs 1 and 2 on, LED 0 off
      mLeds[0].Write(false);
      mLeds[1].Write(true);
      mLeds[2].Write(true);
    } else if (dB >= -20.0f) {
      // >= -20dB: LED 2 on, LEDs 0 and 1 off
      mLeds[0].Write(false);
      mLeds[1].Write(false);
      mLeds[2].Write(true);
    } else {
      // < -20dB: all LEDs off
      mLeds[0].Write(false);
      mLeds[1].Write(false);
      mLeds[2].Write(false);
    }
  }

  void loop() override {
    
    if (usb_connected) {
      indicateLEDs();
    }

    // hardware.SetLed(mWavWriter.recording());
    
    System::Delay(1);
  }
};

void wakeUp() {}

void sleepMode() {
  // Clean D-Cache before entering sleep (recommended by documentation)
  SCB_CleanDCache();
  
  // Configure domains before putting CPU in deep sleep
  SET_BIT(PWR->CPUCR, PWR_CPUCR_PDDS_D1);      // D1 STANDBY
  SET_BIT(PWR->CPUCR, PWR_CPUCR_PDDS_D2);      // D2 STANDBY
  CLEAR_BIT(PWR->CPUCR, PWR_CPUCR_PDDS_D3);    // D3 STOP
  
  // Use low-power regulator as per existing EnterSTOP2Mode function
  MODIFY_REG(PWR->CR1, PWR_CR1_LPDS, PWR_LOWPOWERREGULATOR_ON);
  
  // Set deep sleep bit -> sends CPU to deep sleep after a WFI or WFE
  SET_BIT(SCB->SCR, SCB_SCR_SLEEPDEEP_Msk);

  // Waits for instructions to finish as per existing EnterSTOP2Mode function
  __DSB();
  __ISB();

  // WFI to put into sleep mode until interrupt detected
  __WFI();

  // Wake up
  CLEAR_BIT(SCB->SCR, SCB_SCR_SLEEPDEEP_Msk);

  wakeUp();
}


inline void StartUSBDebounceTimer() {
  Jaffx::Firmware::instance->hardware.PrintLine("Starting USB Debounce Timer");
    TIM13->CNT = 0; // Reset the timer counter
    TIM13->CR1 |= TIM_CR1_CEN; // Start the timer
}

inline void StartSDDebounceTimer() {
  Jaffx::Firmware::instance->hardware.PrintLine("Starting SD Debounce Timer");
    TIM14->CNT = 0; // Reset the timer counter
    TIM14->CR1 |= TIM_CR1_CEN; // Start the timer
}

// Power Button IRQ Handler
extern "C" void EXTI0_IRQHandler(void) {
  // Check if EXTI0 triggered the interrupt
    if (EXTI->PR1 & EXTI_PR1_PR0) {
        /* Clear pending flag */
        EXTI->PR1 |= EXTI_PR1_PR0;

        /* Determine edge by reading input */
        SlipRecorder& mInstance = SlipRecorder::Instance();
        if (GPIOC->IDR & GPIO_IDR_ID0) {
          mInstance.on_PC0_rising();
        }
    }
}

// Done in this order
enum InterruptState {
    FALLING = 0,
    RISING = 1,  
    NONE = 2
};

volatile InterruptState USB_IRQ_State = NONE;

// USB 
extern "C" void EXTI2_IRQHandler(void) {
    // Check if EXTI2 triggered the interrupt
    if (EXTI->PR1 & EXTI_PR1_PR2) {
        // Clear the interrupt pending bit for EXTI2
        EXTI->PR1 |= EXTI_PR1_PR2;

        // Check if the debounce timer is already running
        if (TIM13->CR1 & TIM_CR1_CEN) return;
        
        // Else, determine if it was a rising or falling edge
        if (GPIOA->IDR & GPIO_IDR_ID2) {
            // Rising edge detected
            SlipRecorder::Instance().on_PA2_rising();
            // Save the state of what the new value is and we will see if it's the same as before
            USB_IRQ_State = RISING;
            
        } else {
            // Falling edge detected
            SlipRecorder::Instance().on_PA2_falling();
            // Save the state of what the new value is and we will see if it's the same as before
            USB_IRQ_State = FALLING;
        }
        StartUSBDebounceTimer();
    }
}

// For the USB connection debounce
extern "C" void TIM8_UP_TIM13_IRQHandler(void) {
    // Checks that TIM13 caused the interrupt
    Jaffx::Firmware::instance->hardware.PrintLine("TIM13 IRQ Handler Triggered");
    if (TIM13->SR & TIM_SR_UIF) { // Check update interrupt flag
        TIM13->SR &= ~TIM_SR_UIF; // Clear update interrupt flag

        // Check if the value is still the same as when the timer was started
        if (USB_IRQ_State == NONE) return;
        bool currentState = (GPIOA->IDR & GPIO_IDR_ID2) != 0; // (1 if high, 0 if low)
        if (USB_IRQ_State == RISING && currentState) {
            // We started rising and have settled on rising
            SlipRecorder::Instance().on_PA2_fully_risen();
        }
        else if (USB_IRQ_State == FALLING && !currentState) {
            // We started falling and have settled on falling
            SlipRecorder::Instance().on_PA2_fully_fallen();
        }
        else {
            // State changed during debounce period; no action taken
            Jaffx::Firmware::instance->hardware.PrintLine("USB: Not a valid bounce");
        }
        USB_IRQ_State = NONE;
        TIM13->CR1 &= ~TIM_CR1_CEN; // Stop the timer
    }
}

volatile InterruptState SD_IRQ_State = NONE;
// SD Card Connection Detection IRQ Handler
extern "C" void EXTI15_10_IRQHandler(void) {
    // Check if EXTI12 triggered the interrupt
    if (EXTI->PR1 & EXTI_PR1_PR12) {
        /* Clear pending flag */
        EXTI->PR1 |= EXTI_PR1_PR12;

        // Check if the debounce timer is already running
        if (TIM14->CR1 & TIM_CR1_CEN) return;

        // Else, determine if it was a rising or falling edge
        if (GPIOB->IDR & GPIO_IDR_ID12) {
            // Rising edge detected
            SlipRecorder::Instance().on_PB12_rising();
            // Save the state of what the new value is and we will see if it's the same as before
            SD_IRQ_State = RISING;
        } else {
            // Falling edge detected
            SlipRecorder::Instance().on_PB12_falling();
            // Save the state of what the new value is and we will see if it's the same as before
            SD_IRQ_State = FALLING;
        }
        StartSDDebounceTimer();
    }
}

// For the SD Card connection debounce
extern "C" void TIM8_TRG_COM_TIM14_IRQHandler(void) {
    // Checks that TIM14 caused the interrupt
    if (TIM14->SR & TIM_SR_UIF) { // Check update interrupt flag
        TIM14->SR &= ~TIM_SR_UIF; // Clear update interrupt flag

        if (SD_IRQ_State == NONE) return;
        bool currentState = (GPIOB->IDR & GPIO_IDR_ID12) != 0; // (1 if high, 0 if low)
        if (SD_IRQ_State == RISING && currentState) {
            // We started rising and have settled on rising
            SlipRecorder::Instance().on_PB12_fully_risen();
        }
        else if (SD_IRQ_State == FALLING && !currentState) {
            // We started falling and have settled on falling
            SlipRecorder::Instance().on_PB12_fully_fallen();
        }
        else {
            // State changed during debounce period; no action taken
            Jaffx::Firmware::instance->hardware.PrintLine("SD Card: Not a valid bounce");
        }

        SD_IRQ_State = NONE;
        TIM14->CR1 &= ~TIM_CR1_CEN; // Stop the timer
    }
}

int main() {
  SlipRecorder::Instance().start();
  // EXTIptr = mSlipRecorder::IRQHandler
  return 0;
}