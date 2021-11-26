#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <crypt.h>

int
main(int argc, char *argv[])
{
  char *password = "abcd";
  char *salt = "hi";
  int opt;
  opterr = 1;
  while ((opt = getopt(argc, argv, "p:s:")) != -1)
  {
    switch (opt)
    {
    case 'p':
      password = optarg;
      break;
    case 's':
      salt = optarg;
      break;
    default:
      exit(1);
      break;
    }
  }

  char *hash = crypt(password, salt);
  printf("%s\n", hash);
  return 0;
}
