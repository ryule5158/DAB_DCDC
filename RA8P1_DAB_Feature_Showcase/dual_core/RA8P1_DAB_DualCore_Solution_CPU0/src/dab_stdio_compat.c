#include "dab_stdio_compat.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

#define DAB_STDIO_FORMAT_BUFFER_BYTES    (640U)

int _write(int fd, char * p_buffer, int size);

int dab_stdio_printf(char const * p_format, ...)
{
    if (NULL == p_format)
    {
        return 0;
    }

    char buffer[DAB_STDIO_FORMAT_BUFFER_BYTES];
    va_list args;
    va_start(args, p_format);
    int const required = vsnprintf(buffer, sizeof(buffer), p_format, args);
    va_end(args);

    if (required <= 0)
    {
        return required;
    }

    size_t const available = sizeof(buffer) - 1U;
    int const bytes = (required < (int) available) ? required : (int) available;
    return _write(1, buffer, bytes);
}
