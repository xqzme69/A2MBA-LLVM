#ifndef A2MBA_SDK_A2MBA_H
#define A2MBA_SDK_A2MBA_H

#if !defined(__clang__)
#error "A2MBA source annotations require Clang"
#endif

#define A2MBA_PROTECT __attribute__((annotate("a2mba")))
#define A2MBA_PROTECT_NOINLINE __attribute__((annotate("a2mba"), noinline))
#define A2MBA_IGNORE __attribute__((annotate("a2mba.ignore")))

#endif
