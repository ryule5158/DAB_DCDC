/* generated vector source file - do not edit */
#include "bsp_api.h"
/* Do not build these data structures if no interrupts are currently allocated because IAR will have build errors. */
#if VECTOR_DATA_IRQ_COUNT > 0
        BSP_DONT_REMOVE const fsp_vector_t g_vector_table[BSP_ICU_VECTOR_NUM_ENTRIES] BSP_PLACE_IN_SECTION(BSP_SECTION_APPLICATION_VECTORS) =
        {
                        [0] = sci_b_uart_rxi_isr, /* SCI9 RXI (Receive data full) */
            [1] = sci_b_uart_txi_isr, /* SCI9 TXI (Transmit data empty) */
            [2] = sci_b_uart_tei_isr, /* SCI9 TEI (Transmit end) */
            [3] = sci_b_uart_eri_isr, /* SCI9 ERI (Receive error) */
            [4] = adc_b_calend0_isr, /* ADC CALEND0 (End of calibration of A/D converter unit 0) */
            [5] = adc_b_calend1_isr, /* ADC CALEND1 (End of calibration of A/D converter unit 1) */
            [6] = iic_master_rxi_isr, /* IIC2 RXI (Receive data full) */
            [7] = iic_master_txi_isr, /* IIC2 TXI (Transmit data empty) */
            [8] = iic_master_tei_isr, /* IIC2 TEI (Transmit end) */
            [9] = iic_master_eri_isr, /* IIC2 ERI (Transfer error) */
            [10] = layer3_switch_gwdi_isr, /* ETHER GWDI0 (GWCA Data Interrupt 0) */
            [11] = layer3_switch_eaei_isr, /* ETHER EAEI0 (ETHA0 Error Interrupt) */
            [12] = layer3_switch_eaei_isr, /* ETHER EAEI1 (ETHA1 Error Interrupt) */
        };
        #if BSP_FEATURE_ICU_HAS_IELSR
        const bsp_interrupt_event_t g_interrupt_event_link_select[BSP_ICU_VECTOR_NUM_ENTRIES] =
        {
            [0] = BSP_PRV_VECT_ENUM(EVENT_SCI9_RXI,GROUP0), /* SCI9 RXI (Receive data full) */
            [1] = BSP_PRV_VECT_ENUM(EVENT_SCI9_TXI,GROUP1), /* SCI9 TXI (Transmit data empty) */
            [2] = BSP_PRV_VECT_ENUM(EVENT_SCI9_TEI,GROUP2), /* SCI9 TEI (Transmit end) */
            [3] = BSP_PRV_VECT_ENUM(EVENT_SCI9_ERI,GROUP3), /* SCI9 ERI (Receive error) */
            [4] = BSP_PRV_VECT_ENUM(EVENT_ADC_CALEND0,GROUP4), /* ADC CALEND0 (End of calibration of A/D converter unit 0) */
            [5] = BSP_PRV_VECT_ENUM(EVENT_ADC_CALEND1,GROUP5), /* ADC CALEND1 (End of calibration of A/D converter unit 1) */
            [6] = BSP_PRV_VECT_ENUM(EVENT_IIC2_RXI,GROUP6), /* IIC2 RXI (Receive data full) */
            [7] = BSP_PRV_VECT_ENUM(EVENT_IIC2_TXI,GROUP7), /* IIC2 TXI (Transmit data empty) */
            [8] = BSP_PRV_VECT_ENUM(EVENT_IIC2_TEI,GROUP0), /* IIC2 TEI (Transmit end) */
            [9] = BSP_PRV_VECT_ENUM(EVENT_IIC2_ERI,GROUP1), /* IIC2 ERI (Transfer error) */
            [10] = BSP_PRV_VECT_ENUM(EVENT_ETHER_GWDI0,GROUP2), /* ETHER GWDI0 (GWCA Data Interrupt 0) */
            [11] = BSP_PRV_VECT_ENUM(EVENT_ETHER_EAEI0,GROUP3), /* ETHER EAEI0 (ETHA0 Error Interrupt) */
            [12] = BSP_PRV_VECT_ENUM(EVENT_ETHER_EAEI1,GROUP4), /* ETHER EAEI1 (ETHA1 Error Interrupt) */
        };
        #endif
        #endif
