#include "drv_tcan3413.h"

bool tcan3413_board_normal_mode(void)
{
    /* Final native netlist: TCAN3413 STB is held LOW by 10 kOhm. */
    return true;
}

char const * tcan3413_board_note(void)
{
    return "TCAN3413 STB=10k pulldown normal mode; TXD/RXD=P704/P705 via 33R; split 2x60.4R termination";
}
