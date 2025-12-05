#include "../../Jaffx.hpp"
// #include "StateMachine.hpp"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include "stm32h750xx.h"
#include "stm32h7xx_hal.h"
#include "stm32h7xx_hal_pwr_ex.h"



// State machine stuffs
enum class SlipRecorderState : unsigned char { // inherit from unsigned char to save space
    RECORDING,
    DEEPSLEEP
};

volatile SlipRecorderState currentState = SlipRecorderState::DEEPSLEEP;


// TODO: Move these to the dedicated DMA section exposed in daisy_core.h
// Global SD resources (hardware-required placement in AXI SRAM for DMA)
SdmmcHandler global_sdmmc_handler __attribute__((section(".sram1_bss")));
FatFSInterface global_fsi_handler __attribute__((section(".sram1_bss")));
FIL global_wav_file __attribute__((section(".sram1_bss")));
#include "SDCard.hpp"
#include "Interrupts.hpp"

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

  inline void deinit() {
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

  inline void shutdownSequence() {
    hardware.PrintLine("Initiating Shutdown Sequence...");
    deinit();
    sleepMode();
  }

  inline void init() override {
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

    // TODO: check this is right???? choose state based on SD status
    if (mWavWriter.sdStatus()) {
        currentState = SlipRecorderState::RECORDING;
    } else {
      currentState = SlipRecorderState::DEEPSLEEP;
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

  inline float processAudio(float in) override {
    // TODO: return in and maybe store this somewhere else
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
    inline void on_PB12_fully_risen() {
        hardware.PrintLine("SD Card Fully Removed");
        switch (currentState) {
            case SlipRecorderState::RECORDING: {
                /**
                 * TODO: Immediately stop recording
                 * Disable everything and shutdown
                 * 
                 */
                mWavWriter.StopRecording();
                sleepMode();
                currentState = SlipRecorderState::DEEPSLEEP;

            }
                break;
            case SlipRecorderState::DEEPSLEEP: {
                /**
                 * Disable everything and shutdown again
                 * 
                 */
            }
                break;
                default: {
                    // TODO: Warn about unexpected state
                }
                    break;
        }
    }

  void on_PB12_falling() {
    hardware.PrintLine("Falling Edge Detected");
  }
    inline void on_PB12_fully_fallen() {
        hardware.PrintLine("SD Card Fully Inserted");
        switch (currentState) {
            case SlipRecorderState::RECORDING: {
                /**
                 * Should not be possible - warn!!!!
                 */

            }
                break;
            case SlipRecorderState::DEEPSLEEP: {
                /**
                 * Start recording again
                 * 
                 */
                if(mWavWriter.sdStatus()) {
                    mWavWriter.StartRecording();
                }
                currentState = SlipRecorderState::RECORDING;
            }
                break;
        }
    }

  void on_PA2_rising() {
      usb_connected = true;
      hardware.PrintLine("USB Connected");
  }
  
  void on_PA2_fully_risen() {
    hardware.PrintLine("USB Fully Connected");
    // TODO: We don't really care about this if we don't use the internal battery
  }

    void on_PA2_falling() {
        usb_connected = false;
        hardware.PrintLine("USB Disconnected");
    }

    void on_PA2_fully_fallen() {
        hardware.PrintLine("USB Fully Disconnected");
        // TODO: We don't really care about this without the battery
    }

    inline void on_PC0_short_press() {
        hardware.PrintLine("Power Button Short-Pressed");
        switch (currentState) {
            case SlipRecorderState::RECORDING: {
                // TODO: We want to save the recording safely and then shutdown gracefully
                currentState = SlipRecorderState::DEEPSLEEP;
            }
            break;
            case SlipRecorderState::DEEPSLEEP: {
                // TODO: We want to reinitialize everything and attempt to start the recording
                // If everything successful:
                currentState = SlipRecorderState::RECORDING;
            }
            break;
            default: {
                // TODO: Warn about unhandled state
            }

        }
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
        switch (currentState) {
            case SlipRecorderState::RECORDING: {
                // TODO: Handle the clip detector LEDs here
                // TODO: (if applies) handle battery states and such here
                }
                break;
            case SlipRecorderState::DEEPSLEEP: {
                // TODO: Nothing much, hbu?
            }
                break;
                default: {
                    // TODO: Warn about unhandled state here
                }
        }
    if (usb_connected) {
      indicateLEDs();
    }

    // hardware.SetLed(mWavWriter.recording());
    
    System::Delay(1);
  }
};

void wakeUp() {
    switch (currentState) {
        case SlipRecorderState::RECORDING: {
            // TODO: This shouldn't be possible, warn about this
        }
        break;
        case SlipRecorderState::DEEPSLEEP: {
            // TODO: Reinitialize and make sure everything is on
            // TODO: Make sure this doesn't clash with the power ON interrupt already
        }
        break;
        default: {
            // TODO: Warn about unhandled case
        }
        break;
    }
}

inline void sleepMode() {
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


volatile uint32_t powerButtonInitiallyPressedAtTimeUs = 0;
volatile bool powerButtonPressedDown = false;
// Power Button IRQ Handler
extern "C" void EXTI0_IRQHandler(void) {
  // Check if EXTI0 triggered the interrupt
    if (EXTI->PR1 & EXTI_PR1_PR0) {
        /* Clear pending flag */
        EXTI->PR1 |= EXTI_PR1_PR0;

        /* Determine edge by reading input */
        SlipRecorder& mInstance = SlipRecorder::Instance();
        if (GPIOC->IDR & GPIO_IDR_ID0) { // Rising edge detected
            // Get System::GetUs(), set bool powerButtonPressActive = true;
            powerButtonInitiallyPressedAtTimeUs = System::GetUs();
            powerButtonPressedDown = true;
        //   mInstance.on_PC0_rising(); // This logic will go in falling edge now
        }
        else { // Falling edge detected
            // If powerButtonPressActive, see if time elapsed >= ~4 sec, reset bool
            //  if it is, run long press code (infinite timeout mode)
            //  else, run short press code (power button pressed rising edge)
            if (powerButtonPressedDown) {
                powerButtonPressedDown = false;
                uint32_t timeNow = System::GetUs();
                uint32_t timeElapsedUs = timeNow - powerButtonInitiallyPressedAtTimeUs;
                if (timeElapsedUs >= 4000000) {
                    // If it was pressed down for more than 4 sec, go into infinite timeout mode
                    System::ResetToBootloader(System::BootloaderMode::DAISY_INFINITE_TIMEOUT);
                }
                else {
                    // Run the normal button pressed
                    mInstance.on_PC0_short_press();
                }
            }
        }
    }
}

// Used to software debounce interrupts (using hardware timers) 
enum class InterruptState : unsigned char { // inherit from unsigned char to save space
    FALLING = 0,
    RISING = 1,  
    NONE = 2
};

volatile InterruptState USB_IRQ_State = InterruptState::NONE;

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
            USB_IRQ_State = InterruptState::RISING;
            
        } else {
            // Falling edge detected
            SlipRecorder::Instance().on_PA2_falling();
            // Save the state of what the new value is and we will see if it's the same as before
            USB_IRQ_State = InterruptState::FALLING;
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
        if (USB_IRQ_State == InterruptState::NONE) return;
        bool currentState = (GPIOA->IDR & GPIO_IDR_ID2) != 0; // (1 if high, 0 if low)
        if (USB_IRQ_State == InterruptState::RISING && currentState) {
            // We started rising and have settled on rising
            SlipRecorder::Instance().on_PA2_fully_risen();
        }
        else if (USB_IRQ_State == InterruptState::FALLING && !currentState) {
            // We started falling and have settled on falling
            SlipRecorder::Instance().on_PA2_fully_fallen();
        }
        else {
            // State changed during debounce period; no action taken
            Jaffx::Firmware::instance->hardware.PrintLine("USB: Not a valid bounce");
        }
        USB_IRQ_State = InterruptState::NONE;
        TIM13->CR1 &= ~TIM_CR1_CEN; // Stop the timer
    }
}

volatile InterruptState SD_IRQ_State = InterruptState::NONE;
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
            SD_IRQ_State = InterruptState::RISING;
        } else {
            // Falling edge detected
            SlipRecorder::Instance().on_PB12_falling();
            // Save the state of what the new value is and we will see if it's the same as before
            SD_IRQ_State = InterruptState::FALLING;
        }
        StartSDDebounceTimer();
    }
}

// For the SD Card connection debounce
extern "C" void TIM8_TRG_COM_TIM14_IRQHandler(void) {
    // Checks that TIM14 caused the interrupt
    if (TIM14->SR & TIM_SR_UIF) { // Check update interrupt flag
        TIM14->SR &= ~TIM_SR_UIF; // Clear update interrupt flag

        if (SD_IRQ_State == InterruptState::NONE) return;
        bool currentState = (GPIOB->IDR & GPIO_IDR_ID12) != 0; // (1 if high, 0 if low)
        if (SD_IRQ_State == InterruptState::RISING && currentState) {
            // We started rising and have settled on rising
            SlipRecorder::Instance().on_PB12_fully_risen();
        }
        else if (SD_IRQ_State == InterruptState::FALLING && !currentState) {
            // We started falling and have settled on falling
            SlipRecorder::Instance().on_PB12_fully_fallen();
        }
        else {
            // State changed during debounce period; no action taken
            Jaffx::Firmware::instance->hardware.PrintLine("SD Card: Not a valid bounce");
        }

        SD_IRQ_State = InterruptState::NONE;
        TIM14->CR1 &= ~TIM_CR1_CEN; // Stop the timer
    }
}

int main() {
  SlipRecorder::Instance().start();
  // EXTIptr = mSlipRecorder::IRQHandler
  return 0;
}