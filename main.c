#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>

enum brute_mode_t
{
  M_RECURSIVE,
  M_ITERATIVE
};

struct config_t
{
  char *alphabet;
  int length;
  enum brute_mode_t mode;
};

void
bruteforce_rec(char *password, struct config_t *config, int pos)
{
  if (config->length == pos)
    printf("r %s\n", password);
  else
  {
    for (int i = 0; config->alphabet[i] != '\0'; ++i)
    {
      password[pos] = config->alphabet[i];
      bruteforce_rec(password, config, pos + 1);
    }
  }
}

void
bruteforce_iter(char *password, struct config_t *config)
{
  size_t size = strlen(config->alphabet) - 1;
  int a[config->length];
  memset(a, 0, config->length * sizeof(int));

  while (true)
  {
    int k;
    for (k = config->length - 1; (k >= 0) && (a[k] == size); --k)
      a[k] = 0;
    if (k < 0) break;
    a[k]++;
    for (k = 0; k < config->length; ++k)
      password[k] = config->alphabet[a[k]];

    printf("i %s\n", password);
  }
}

void
parse_opts(struct config_t *config, int argc, char *argv[])
{
  int opt;
  opterr = 1;
  while ((opt = getopt(argc, argv, "ira:l:")) != -1)
  {
    switch (opt)
    {
    case 'i':
      config->mode = M_ITERATIVE;
      break;
    case 'r':
      config->mode = M_RECURSIVE;
      break;
    case 'a':
      config->alphabet = optarg;
      break;
    case 'l':
      config->length = atoi(optarg);
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
    .mode = M_ITERATIVE
  };
  parse_opts(&config, argc, argv);

  char password[config.length + 1];
  password[config.length] = '\0';
  
  switch (config.mode)
  {
  case M_ITERATIVE:
    bruteforce_iter(password, &config);
    break;
  case M_RECURSIVE:
    bruteforce_rec(password, &config, 0);
    break;
  }

  return 0;
}
