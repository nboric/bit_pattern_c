// we only use std library for reading /dev/random, printing output, and profiling,
// not for solution itself
#include <stdio.h>
#include <time.h>

#define pattern 0b110
#define N_BYTES 10000000
#define BATCH_SIZE 1000

// #define DEBUG

// helper function to profile
int64_t timespec_delta(const struct timespec after, const struct timespec before)
{
    return ((int64_t)after.tv_sec - (int64_t)before.tv_sec) * (int64_t)1000000000
        + ((int64_t)after.tv_nsec - (int64_t)before.tv_nsec);
}

struct method1_state
{
    int pos;
};

struct method2_state
{
    unsigned char prev;
};

struct method3_state
{
    unsigned char prev;
    int count_lut[1024]; // 10 bits
};

// state machine
int method1_pattern_match(void* generic_state, const unsigned char sample)
{
    struct method1_state* state = generic_state;
    int counter = 0;
    for (int i = 7; i >= 0; i--)
    {
        unsigned char bit = sample & (1 << i);
        switch (state->pos)
        {
        case 0:
        case 1:
            if (bit)
            {
                // transition to first or second 1
                state->pos++;
            }
            else
            {
                // restart
                state->pos = 0;
            }
            break;
        case 2:
            if (bit)
            {
                // we saw 111, therefore we still have two 1's, stay in same pos
            }
            else
            {
                // match
                counter++;
                state->pos = 0;
            }
            break;
        default:
            // bug
            break;
        }
    }
    return counter;
}

// sliding bitmask
int method2_pattern_match(void* generic_state, const unsigned char sample)
{
    struct method2_state* state = generic_state;
    int counter = 0;
    unsigned short combined_samples = ((unsigned short)state->prev << 8) | sample;
    // we need two bits from previous sample, and we look at three bits at a time
    // [... 9 8][7 6 5 4 3 2 1 0]
    // so first time we shift 7 times to get:
    // 9 8 7]
    // last time we shift 0:
    // 2 1 0]
    for (int i = 7; i >= 0; i--)
    {
        if (((combined_samples >> i) & 0x07) == pattern)
        {
            counter++;
        }
    }
    state->prev = sample;
    return counter;
}

void method3_init(struct method3_state* state)
{
    unsigned short combined_samples = 0;
    // build lut
    while (combined_samples < sizeof(state->count_lut) / sizeof(state->count_lut[0]))
    {
        struct method2_state m2_state = {.prev = 0x00};
        // we reuse method 2, first pass MSB
        int counter = method2_pattern_match(&m2_state, combined_samples >> 8);
        // now pass LSB:
        counter += method2_pattern_match(&m2_state, combined_samples & 0xFF);
        state->count_lut[combined_samples] = counter;
        combined_samples++;
    }
}

// LUT
int method3_pattern_match(void* generic_state, const unsigned char sample)
{
    struct method3_state* state = generic_state;
    int counter = 0;
    unsigned short combined_samples = (((unsigned short)state->prev << 8) | sample) & 0x3FF;
    counter = state->count_lut[combined_samples];
    state->prev = sample;
    return counter;
}

struct method
{
    int (*pattern_match_fn)(void* state, unsigned char sample);
    void* state;
    int total_count;
    const char* name;
    int64_t total_time;
};

int main(void)
{
    struct method1_state m1_state = {.pos = 0};
    struct method2_state m2_state = {.prev = 0x00};
    struct method3_state m3_state = {.prev = 0x00};
    method3_init(&m3_state);
    struct timespec start, end;

    struct method methods[3] =
    {
        {
            .pattern_match_fn = method1_pattern_match, .state = &m1_state, .name = "StateMachine", .total_count = 0,
            .total_time = 0
        },
        {
            .pattern_match_fn = method2_pattern_match, .state = &m2_state, .name = "SlidingBitmask", .total_count = 0,
            .total_time = 0
        },
        {
            .pattern_match_fn = method3_pattern_match, .state = &m3_state, .name = "LUT", .total_count = 0,
            .total_time = 0
        },
    };

    // this is just so that we can precalculate the stream and measure time accurately by calling each method many times,
    // instead of measuring every entry into the method, which would be too imprecise
    // the methods themselves work with streamed data
    for (int batch = 0; batch < N_BYTES / BATCH_SIZE; batch++)
    {
        FILE* file = fopen("/dev/random", "r");
        unsigned char samples[BATCH_SIZE];
        for (int i = 0; i < BATCH_SIZE; i++)
        {
            samples[i] = fgetc(file);
        }
        fclose(file);

        for (int method = 0; method < 3; method++)
        {
            clock_gettime(CLOCK_MONOTONIC, &start);
            for (int i = 0; i < BATCH_SIZE; i++)
            {
#ifdef DEBUG
                printf("new sample: 0x%.2x\n", samples[i]);
                for (int j = 7; j >= 0; j--)
                {
                    printf("%d%s", (samples[i]>>j) & 0x01, (j == 0) ? "\n" : " ");
                }
#endif
                int count = methods[method].pattern_match_fn(methods[method].state, samples[i]);
                methods[method].total_count += count;
#ifdef DEBUG
                printf("Method %d count: %d\n", method, count);
#endif
            }
            clock_gettime(CLOCK_MONOTONIC, &end);
            methods[method].total_time += timespec_delta(end, start);
        }
    }

    for (int method = 0; method < 3; method++)
    {
        printf("Method %s total count: %d, time: %.2f ms\n", methods[method].name, methods[method].total_count,
               methods[method].total_time / 1000000.0);
    }
    return 0;
}



