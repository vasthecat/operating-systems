#include "common.h"
#include "singlethreaded.h"
#include "multithreaded.h"
#include "generator.h"

#include "client.h"
#include "server.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <getopt.h>

void
parse_opts(struct config_t *config, int argc, char *argv[])
{
    int opt;
    opterr = 1;
    while ((opt = getopt(argc, argv, "irymsgxca:l:h:")) != -1)
    {
        switch (opt)
        {
        case 'i':
            config->brute_mode = M_ITERATIVE;
            break;
        case 'r':
            config->brute_mode = M_RECURSIVE;
            break;
        case 'y':
            config->brute_mode = M_REC_ITERATOR;
#ifdef __APPLE__
            printf("Recursive iterator mode is not supported on this platform\n");
            exit(EXIT_FAILURE);
#endif
            break;
        case 'a':
            config->alphabet = optarg;
            break;
        case 'l':
            config->length = atoi(optarg);
            break;
        case 'h':
            config->hash = optarg;
            break;
        case 's':
            config->run_mode = M_SINGLE;
            break;
        case 'm':
            config->run_mode = M_MULTI;
            break;
        case 'g':
            config->run_mode = M_GENERATOR;
            break;
        case 'x':
            config->run_mode = M_SERVER;
            break;
        case 'c':
            config->run_mode = M_CLIENT;
            break;
        default:
            exit(1);
            break;
        }
    }
}

int
main(int argc, char *argv[])
{
    struct config_t config = {
        .alphabet = "abc",
        .length = 3,
        .brute_mode = M_ITERATIVE,
        .run_mode = M_SINGLE,
        .hash = "hiwMxUWeODzGE", // hi + ccc
    };
    parse_opts(&config, argc, argv);

    struct task_t task;
    task.password[config.length] = '\0';

    bool found;
    switch (config.run_mode)
    {
    case M_SINGLE:
        found = singlethreaded(&task, &config);
        break;
    case M_MULTI:
        found = multithreaded(&task, &config);
        break;
    case M_GENERATOR:
        found = generator(&task, &config);
        break;
    case M_SERVER:
        found = run_server(&task, &config);
        break;
    case M_CLIENT:
        found = run_client(&task, &config);
        break;
    }

    if (found)
        printf("Password found: '%s'\n", task.password);
    else
        printf("Password not found\n");

    return 0;
}
