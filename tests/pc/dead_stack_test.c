/* PLATFORM_SCRUB_DEAD_STACK (platform.h): what a call below a function leaves
 * on the stack is gone when that function has run the macro, and stays when
 * it has not (the control, so the test sees what it is looking for). A call
 * that dirties the stack stands for the host code a game call runs on the
 * game stack: the clock's passes, the VSync's present. A later call's
 * locals that nothing wrote read it, as a primitive's padding bytes do. */
#include "pc/platform/platform.h"
#include <stdio.h>

/* The test reads the stack of a call that has returned, which AddressSanitizer's
 * frames (redzones, fake stacks) make meaningless: it does not run there. */
#if defined(__SANITIZE_ADDRESS__)
#define SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define SANITIZED 1
#endif
#endif

#define PATTERN 0xA5
#define DIRTY_BYTES 3000u
#define SCRUB_BYTES 8192u

static int scrubbable = 1;
static int asked;

int Platform_DeadStackScrubbable(const void *frame, size_t bytes)
{
    (void)frame;
    (void)bytes;
    asked++;
    return scrubbable;
}

/* Host code, deep below its caller: fills its frame. */
static void __attribute__((noinline)) dirty(void)
{
    volatile unsigned char frame[DIRTY_BYTES];
    unsigned i;
    for (i = 0; i < sizeof(frame); i++) {
        frame[i] = PATTERN;
    }
}

/* A game call's entry: the host code is below it. */
static void __attribute__((noinline)) entry(int scrub)
{
    dirty();
    if (scrub) {
        PLATFORM_SCRUB_DEAD_STACK(SCRUB_BYTES);
    }
}

/* How many bytes of `bytes` hold what the host code left. */
static unsigned __attribute__((noinline)) count_pattern(volatile unsigned char *bytes, size_t length)
{
    size_t i;
    unsigned count = 0;
    for (i = 64; i < length; i++) {
        if (bytes[i] == PATTERN) {
            count++;
        }
    }
    return count;
}

/* The game's next call: a local nothing wrote, read as the game's code would
 * read a primitive's padding. */
static unsigned __attribute__((noinline)) left_over(void)
{
    volatile unsigned char frame[DIRTY_BYTES];
    return count_pattern(frame, sizeof(frame));
}

static int __attribute__((unused)) run(int scrub, unsigned *count)
{
    entry(scrub);
    *count = left_over();
    return 0;
}

int main(void)
{
#ifdef SANITIZED
    puts("dead stack test skipped: AddressSanitizer");
    return 0;
#else
    unsigned control, scrubbed, refused;
    int failures = 0;

    run(0, &control);
    run(1, &scrubbed);
    scrubbable = 0;
    run(1, &refused);
    scrubbable = 1;

    printf("left over: %u bytes without the scrub, %u with it, %u when it was not allowed\n", control, scrubbed,
           refused);
    if (control < DIRTY_BYTES / 2) {
        printf("FAIL: the control saw too little of the host code's frame (%u of %u): nothing to test\n", control,
               DIRTY_BYTES);
        failures++;
    }
    /* A pointer left in the span above the cleared one can hold a pattern byte by chance. */
    if (scrubbed > DIRTY_BYTES / 50) {
        printf("FAIL: the scrub left %u bytes of the host code's frame\n", scrubbed);
        failures++;
    }
    if (refused < DIRTY_BYTES / 2) {
        printf("FAIL: the scrub ran where Platform_DeadStackScrubbable said no (%u left)\n", refused);
        failures++;
    }
    if (asked != 2) {
        printf("FAIL: the macro did not ask (%d)\n", asked);
        failures++;
    }
    puts(failures ? "FAILED" : "dead stack test passed");
    return failures ? 1 : 0;
#endif
}
