/**
 * @file csim.c
 * @brief Cache simulator
 *
 * You can use this program by running it with the following command line
 * arguments:
 *
 *   @param[in]     s       After flag -s, number of set index bits
 *   @param[in]     b       After flag -b, number of block bits
 *   @param[in]     E       After flag -E, number of lines per set
 *   @param[in]    trace    After flag -t, file name of the memory trace to
 *                          process
 *
 * The -s, -b, -E, and -t options must be supplied for all simulations.
 * Additionally, you can supply the -h flag which acts as a help flag and
 * summarizes the parameters needed.
 * You can also supply the -v flag which is not implemented for this simulator
 * but could act as verbose mode if provided.
 *
 * The final output of running this file with the necessary arguments is a
 * number of statistics about the cache, including hits, misses, evictions,
 * dirty bytes, and evicted dirty bytes.
 *
 * Program design:
 *   - The cache is structured as a flat 1D array in set major order with the
 *     most recently accessed lines toward the end of the set and the least
 *     recently accessed at the beginning of the set for easy LRU replacement
 *   - Each cache line is a struct with an integer to determine if the stored
 *     address is valid, an unsigned long long to store the tag that is
 *     currently in the line, and an integer to determine if the line has dirty
 *     bytes to make all statistics easy to calculate
 *   - There is another struct for the stats defined in the file cachelab.h and
 *     the cachelab.c file contains the printSummary function which is used by
 *     the program in the case of a successful simulation
 *
 * Simulator restrictions:
 *   - s + b must be less than 64 because there are 64 bits in an address
 *   - s and b must be positive
 *   - E must be greater than 0
 *
 * @author Ananya Parikh <aparikh2@andrew.cmu.edu>
 */

#include "cachelab.h"
#include <errno.h>
#include <getopt.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define LINELEN 30
#define MAXADDR 0xFFFFFFFFFFFFFFFF
#define MAXSIZE 1024
#define MAXBITS 64

/**
 * @brief The struct for the cache lines.
 *
 * This struct has a integer to assess if the cache line is valid and another
 * integer to determine if the cache line has dirty bytes. These integers are 0
 * if the property is not currently true or 1 if the property is true. It also
 * stores the tag that is currently stored in the line.
 */
typedef struct cache_line {
    int valid;
    unsigned long long tag;
    int dirty;
} cache_line_t;

typedef cache_line_t line;

/**
 * @brief Default message for incorrect arguments or when the user provides the
 *        help flag.
 */
void help(void) {
    printf("Usage: ./csim [-v] -s <s> -E <E> -b <b> -t <trace>\n");
    printf("       ./csim -h\n\n");
    printf("    -h           Print this help message and exit\n");
    printf("    -v           Verbose mode: report effects of each memory "
           "operation\n");
    printf("    -s <s>       Number of set index bits (there are 2**s sets)\n");
    printf("    -b <b>       Number of block bits (there are 2**b blocks)\n");
    printf("    -E <E>       Number of lines per set (associativity)\n");
    printf("    -t <trace>   File name of the memory trace to process\n\n");
    printf("The -s, -b, -E, and -t options must be supplied for all "
           "simulations.\n");
}

/**
 * @brief Cache simulation with statistics updates.
 *
 * This function is the cache simulator that updates the statistics for the
 * simulation with the arguments provided by the user. Use this when wanting to
 * update the statistics based on user input while parsing a file. This
 * function is called by parse_trace_file and uses some information given by
 * parsing the file.
 *
 *   @param[in]     cache   The cache being used for the simulation
 *   @param[in]     tag     The tag for the current address
 *   @param[in]     set     The set for the current address
 *   @param[in]     E       The associatvity of the cache
 *   @param[in]     B       The number of bytes per block
 *   @param[in]     stats   The simulation statistics to be updated
 *   @param[in]     op      The current operation
 */
void simulate(line *cache, unsigned long long tag, unsigned long long set,
              int E, int B, csim_stats_t *stats, char *op) {
    line *cache_set = cache + set * (unsigned long)E;
    int res = -1;

    /* These two variables are declared outside of their for loops
    so that they can be used in later parts of the code.
    The main purpose is so that we know where to place the next address
    depending on how many lines are currently filled in a set.
    */
    int j = 0;
    int k = 0;

    for (int i = 0; i < E; i++) {
        if ((cache_set + i)->valid == 0) {
            res = 0;
            j = i;
            /* We break out of the loop here because we know that we have
            not yet found a line that matches our tag and the rest of the
            lines are invalid due to the structure of our cache. This means
            that we have a miss.
            */
            break;
        } else {
            if ((cache_set + i)->tag == tag) {
                stats->hits++;
                /* This loop moves all of the lines left by 1 to maintain
                the correct order for the LRU replacement policy and shifts
                the line with our tag to the very end since it is now the most
                recently accessed.
                */
                for (j = i; (j < E - 1) && ((cache_set + j + 1)->valid == 1);
                     j++) {
                    (cache_set + j)->tag = (cache_set + j + 1)->tag;
                    (cache_set + j)->dirty = (cache_set + j + 1)->dirty;
                }
                (cache_set + j)->tag = tag;
                if (strcmp(op, "S") == 0) {
                    (cache_set + j)->dirty = 1;
                }
                return;
            }
        }
    }
    if (res == 0) {
        stats->misses++;
        (cache_set + j)->tag = tag;
        (cache_set + j)->valid = 1;
        if (strcmp(op, "S") == 0) {
            (cache_set + j)->dirty = 1;
        }
    }
    if (res == -1) {
        stats->misses++;
        stats->evictions++;
        if (cache_set->dirty == 1) {
            stats->dirty_evictions += (unsigned long)B;
        }
        /* This loops moves all lines left by 1 and gets rid of the least
        recently used line which is the very first one in the set.
        */
        for (k = 0; k < E - 1; k++) {
            (cache_set + k)->tag = (cache_set + k + 1)->tag;
            (cache_set + k)->dirty = (cache_set + k + 1)->dirty;
        }
        (cache_set + k)->tag = tag;
        (cache_set + k)->dirty = 0;
        if (strcmp(op, "S") == 0) {
            (cache_set + k)->dirty = 1;
        }
    }
}

/**
 * @brief Process a memory-access trace file.
 *
 * This function goes through the provided trace file to search for any
 * errors and simulate the cache if no error is found for the current line.
 *
 * If an error is ever found, the parsing immediately ends and goes back to
 * the main function.
 *
 *   @param[in]     cache   The cache being used for the simulation
 *   @param[in]     s       The number of set bits
 *   @param[in]     E       The associatvity of the cache
 *   @param[in]     b       The number of block bits
 *   @param[in]     B       The number of bytes per block
 *   @param[in]     trace   Name of the trace file to process
 *   @param[in]     stats   The simulation statistics to be updated
 *   @return                0 if successful, 1 if there were errors
 */
int process_trace_file(line *cache, int s, int E, int b, int B,
                       const char *trace, csim_stats_t *stats) {
    FILE *tfp = fopen(trace, "rt");

    /* Make sure that the file is valid before parsing it
    to prevent an error.
    */
    if (!tfp) {
        fprintf(stderr, "Error opening '%s': %s\n", trace, strerror(errno));
        return 1;
    }
    char linebuf[LINELEN];
    int parse_error = 0;

    /* Process each line and ensure that they all have the 3 necessary
    components (operations, address, and size) with valid values.
    */
    while (fgets(linebuf, LINELEN, tfp)) {
        if (strchr(linebuf, '\n') == NULL) {
            return 1;
        }

        char *op = strtok(linebuf, " ");
        if (strcmp(op, "L") != 0 && strcmp(op, "S") != 0) {
            return 1;
        }

        char *addr_str = strtok(NULL, ",");
        char *size_str = strtok(NULL, "\n");
        if (addr_str == NULL || size_str == NULL) {
            return 1;
        }

        /* We use strtoull and strtoul here in order to convert the address
        and the size in the current line into numbers, making sure that
        the result is valid.
        */
        char *endptr;
        unsigned long long addr = strtoull(addr_str, &endptr, 16);
        if (*endptr != '\0' || addr > MAXADDR) {
            return 1;
        }

        unsigned long size = strtoul(size_str, &endptr, 10);
        if (*endptr != '\0' || size > MAXSIZE) {
            return 1;
        }

        /* Obtain the tag and the set from the address by utilizing
        bit shifting (to be used in the simulation).
        */
        unsigned long long tag = addr >> (s + b);
        unsigned long long set;
        if (s == 0) {
            set = 0;
        } else {
            set = (addr << (MAXBITS - s - b)) >> (MAXBITS - s);
        }

        simulate(cache, tag, set, E, B, stats, op);
    }
    fclose(tfp);
    return parse_error;
}

/**
 * @brief The main function that runs when the program is called.
 *
 * This function first processes the command-line arguments and sets the
 * proper variables to the inputted values. It then makes sure the
 * arguments are valid. Next, the cache and stats are allocated and
 * initialized. Finally, the function parses the trace file and simulates the
 * cache, printing the summary of the statistics if no errors incurred.
 *
 * If an error is found in command-line arguments, allocations, or parsing
 * the trace file, the function stops running and exit(1) is called.
 *
 *   @param[in]     argc    The number of command-line arguments provided
 *   @param[in]     argv    Array of character pointers where each element
 *                          points to a command-line argument
 *   @return                exit(0) if successful, exit(1) if there were errors
 */
int main(int argc, char **argv) {
    int s = 0, S, E = 0, b = 0, B;
    int opt;
    char *trace_file = NULL;

    /* Process the command line arguments and ensure that all of the
    necessary arguments are given, and if not then describe the issue
    and terminate the program.
    */
    while ((opt = getopt(argc, argv, "vhs:b:E:t:")) != -1) {
        switch (opt) {
        case 'h':
            help();
            exit(0);
            break;
        case 'v':
            break;
        case 's':
            s = atoi(optarg);
            S = (int)pow(2, s);
            break;
        case 'b':
            b = atoi(optarg);
            B = (int)pow(2, b);
            break;
        case 'E':
            E = atoi(optarg);
            break;
        case 't':
            trace_file = optarg;
            break;
        default:
            printf("Error while parsing arguments.\n\n");
            help();
            exit(1);
        }
    }
    if ((s + b) > 64) {
        printf("Error: s + b is too large\n");
        exit(1);
    }

    if (s < 0 || E <= 0 || b < 0 || trace_file == NULL) {
        printf("Mandatory arguments missing or zero.\n\n");
        help();
        exit(1);
    }

    /* The cache is represented as a 1D flattened array in set major order and
    all lines are initially invalid and not dirty.
    */
    line *cache =
        (line *)malloc(sizeof(line) * (unsigned long)S * (unsigned long)E);
    if (cache == NULL) {
        printf("Allocation error.\n");
        exit(1);
    }
    for (int i = 0; i < S * E; i++) {
        (cache + i)->valid = 0;
        (cache + i)->dirty = 0;
        (cache + i)->tag = MAXADDR;
    }

    csim_stats_t *stats = malloc(sizeof(csim_stats_t));
    if (stats == NULL) {
        printf("Allocation error.\n");
        exit(1);
    }
    stats->hits = 0;
    stats->misses = 0;
    stats->evictions = 0;
    stats->dirty_bytes = 0;
    stats->dirty_evictions = 0;

    if (process_trace_file(cache, s, E, b, B, trace_file, stats) == 1) {
        printf("Error while parsing trace file.\n");
        exit(1);
    } else {
        for (int i = 0; i < S * E; i++) {
            if ((cache + i)->dirty == 1) {
                stats->dirty_bytes += (unsigned long)B;
            }
        }
        printSummary(stats);
    }

    free(cache);
    free(stats);
    exit(0);
}
