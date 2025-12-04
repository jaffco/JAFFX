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


// For the USB connection detection
void PA2_EXTI_Init(void) {
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

// For the power button
void PC0_EXTI_Init(void) {
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

// For the SD Card connection detection
void PB12_EXTI_Init(void) {
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
  float RmsReport = 0.f;
  SDCardWavWriter<> mWavWriter;  // Use default template parameter (4096 bytes)
  bool usb_connected = false;
  StateMachine mStateMachine;


  void init() override {
    debug = true;
    // Initialize LEDs
    mLeds[0].Init(seed::D21, GPIO::Mode::OUTPUT);
    mLeds[1].Init(seed::D20, GPIO::Mode::OUTPUT);
    mLeds[2].Init(seed::D19, GPIO::Mode::OUTPUT);
    
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
    PB12_EXTI_Init();
    PC0_EXTI_Init();
    PA2_EXTI_Init();
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

  void on_PB12_falling() {
    hardware.PrintLine("Falling Edge Detected");
  }

  void on_PA2_rising() {
      usb_connected = true;
      hardware.PrintLine("USB Connected");
  }

    void on_PA2_falling() {
        usb_connected = false;
        hardware.PrintLine("USB Disconnected");
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



// (*EXTIptr)()
bool toggle = false; // global toggle? sus af

extern "C" void EXTI15_10_IRQHandler(void) {
  // if (EXTI->PR1 & EXTI_PR1_PR12) {
    /* Clear pending flag */
    EXTI->PR1 |= EXTI_PR1_PR12;

   Jaffx::Firmware::instance->hardware.SetLed(toggle);
    toggle = !toggle;

    /* Determine edge by reading input */
    SlipRecorder& mInstance = SlipRecorder::Instance();
    if (GPIOB->IDR & GPIO_IDR_ID12) {
      mInstance.on_PB12_rising();
    } else {
      mInstance.on_PB12_falling();
    }
  //}
}

extern "C" void EXTI0_IRQHandler(void) {
    if (EXTI->PR1 & EXTI_PR1_PR0) {
        /* Clear pending flag */
        EXTI->PR1 |= EXTI_PR1_PR0;

       Jaffx::Firmware::instance->hardware.SetLed(toggle);
        toggle = !toggle;

        /* Determine edge by reading input */
        SlipRecorder& mInstance = SlipRecorder::Instance();
        if (GPIOC->IDR & GPIO_IDR_ID0) {
          mInstance.on_PC0_rising();
        }
    }
}

extern "C" void EXTI2_IRQHandler(void) {
    // Check if EXTI2 triggered the interrupt
    if (EXTI->PR1 & EXTI_PR1_PIF2) {
        // Clear the interrupt pending bit for EXTI2
        EXTI->PR1 |= EXTI_PR1_PIF2;
        
        // Determine if it was a rising or falling edge
        if (GPIOA->IDR & GPIO_IDR_ID2) {
            // Rising edge detected
            SlipRecorder::Instance().on_PA2_rising();
        } else {
            // Falling edge detected
            SlipRecorder::Instance().on_PA2_falling();
        }
    }
}


int main() {
  SlipRecorder::Instance().start();
  // EXTIptr = mSlipRecorder::IRQHandler
  return 0;
}