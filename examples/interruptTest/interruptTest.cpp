
#include "stm32l476xx.h"



void PB12_EXTI_Init(void) {
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

void EXTI15_10_IRQHandler(void) {

}




int main() {
	EXTI_Init();
	
	while(1);
	
	return 0;
}
