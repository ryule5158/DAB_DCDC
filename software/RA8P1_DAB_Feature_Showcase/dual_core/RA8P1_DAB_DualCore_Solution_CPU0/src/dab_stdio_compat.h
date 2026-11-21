#ifndef DAB_STDIO_COMPAT_H
#define DAB_STDIO_COMPAT_H

/*
 * LLVM's embedded stdio archive expects a hosted `stdout` object.  This
 * firmware already owns the UART9 transport through _write(), so keep the
 * observable logging path identical and bypass hosted FILE streams.
 */
int dab_stdio_printf(char const * p_format, ...);

#endif /* DAB_STDIO_COMPAT_H */
