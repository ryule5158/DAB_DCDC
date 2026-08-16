/* generated vector source file - do not edit */
        #include "bsp_api.h"
        /* Do not build these data structures if no interrupts are currently allocated because IAR will have build errors. */
        #if VECTOR_DATA_IRQ_COUNT > 0
        BSP_DONT_REMOVE const fsp_vector_t g_vector_table[BSP_ICU_VECTOR_NUM_ENTRIES] BSP_PLACE_IN_SECTION(BSP_SECTION_APPLICATION_VECTORS) =
        {
                        [0] = usbfs_interrupt_handler, /* USBFS INT (USBFS interrupt) */
            [1] = usbfs_resume_handler, /* USBFS RESUME (USBFS resume interrupt) */
            [2] = sci_b_uart_rxi_isr, /* SCI9 RXI (Receive data full) */
            [3] = sci_b_uart_txi_isr, /* SCI9 TXI (Transmit data empty) */
            [4] = sci_b_uart_tei_isr, /* SCI9 TEI (Transmit end) */
            [5] = sci_b_uart_eri_isr, /* SCI9 ERI (Receive error) */
            [6] = rm_ethosu_isr, /* NPU IRQ (NPU IRQ) */
            [7] = adc_b_calend0_isr, /* ADC CALEND0 (End of calibration of A/D converter unit 0) */
            [8] = adc_b_calend1_isr, /* ADC CALEND1 (End of calibration of A/D converter unit 1) */
            [9] = iic_master_rxi_isr, /* IIC2 RXI (Receive data full) */
            [10] = iic_master_txi_isr, /* IIC2 TXI (Transmit data empty) */
            [11] = iic_master_tei_isr, /* IIC2 TEI (Transmit end) */
            [12] = iic_master_eri_isr, /* IIC2 ERI (Transfer error) */
        };
        #if BSP_FEATURE_ICU_HAS_IELSR
        const bsp_interrupt_event_t g_interrupt_event_link_select[BSP_ICU_VECTOR_NUM_ENTRIES] =
        {
            [0] = BSP_PRV_VECT_ENUM(EVENT_USBFS_INT,GROUP0), /* USBFS INT (USBFS interrupt) */
            [1] = BSP_PRV_VECT_ENUM(EVENT_USBFS_RESUME,GROUP1), /* USBFS RESUME (USBFS resume interrupt) */
            [2] = BSP_PRV_VECT_ENUM(EVENT_SCI9_RXI,GROUP2), /* SCI9 RXI (Receive data full) */
            [3] = BSP_PRV_VECT_ENUM(EVENT_SCI9_TXI,GROUP3), /* SCI9 TXI (Transmit data empty) */
            [4] = BSP_PRV_VECT_ENUM(EVENT_SCI9_TEI,GROUP4), /* SCI9 TEI (Transmit end) */
            [5] = BSP_PRV_VECT_ENUM(EVENT_SCI9_ERI,GROUP5), /* SCI9 ERI (Receive error) */
            [6] = BSP_PRV_VECT_ENUM(EVENT_NPU_IRQ,GROUP6), /* NPU IRQ (NPU IRQ) */
            [7] = BSP_PRV_VECT_ENUM(EVENT_ADC_CALEND0,GROUP7), /* ADC CALEND0 (End of calibration of A/D converter unit 0) */
            [8] = BSP_PRV_VECT_ENUM(EVENT_ADC_CALEND1,GROUP0), /* ADC CALEND1 (End of calibration of A/D converter unit 1) */
            [9] = BSP_PRV_VECT_ENUM(EVENT_IIC2_RXI,GROUP1), /* IIC2 RXI (Receive data full) */
            [10] = BSP_PRV_VECT_ENUM(EVENT_IIC2_TXI,GROUP2), /* IIC2 TXI (Transmit data empty) */
            [11] = BSP_PRV_VECT_ENUM(EVENT_IIC2_TEI,GROUP3), /* IIC2 TEI (Transmit end) */
            [12] = BSP_PRV_VECT_ENUM(EVENT_IIC2_ERI,GROUP4), /* IIC2 ERI (Transfer error) */
        };
        #endif
        #endif