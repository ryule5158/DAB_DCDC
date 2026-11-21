#ifndef DAB_RA8P1_SCOPE_TEST_H
#define DAB_RA8P1_SCOPE_TEST_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Runtime-isolated eight-PWM scope test.
 * Enter/exit never asserts RUN_REQ or any power-path control.
 */
void dab_ra8p1_scope_test_enter(void);
bool dab_ra8p1_scope_test_process_char(uint8_t ch);
void dab_ra8p1_scope_test_exit(void);

#endif /* DAB_RA8P1_SCOPE_TEST_H */
