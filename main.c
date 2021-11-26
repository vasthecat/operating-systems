#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>

void
bruteforce_rec(char *password, char *alphabet, int length, int pos)
{
  if (length == pos)
    printf("%s\n", password);
  else
  {
    for (int i = 0; alphabet[i] != '\0'; ++i)
    {
      password[pos] = alphabet[i];
      bruteforce_rec(password, alphabet, length, pos + 1);
    }
  }
}

void
bruteforce_iter(char *password, char *alphabet, int length)
{
  size_t size = strlen(alphabet) - 1;
  int a[length];
  memset(a, 0, length * sizeof(int));

  while (true)
  {
    int k;
    for (k = length - 1; (k >= 0) && (a[k] == size); --k)
      a[k] = 0;
    if (k < 0) break;
    a[k]++;
    for (k = 0; k < length; ++k)
      password[k] = alphabet[a[k]];

    printf("%s\n", password);
  }
}

int
main()
{
  char *alphabet = "abc";
  int length = 4;
  char password[length + 1];
  password[length] = '\0';
  /* bruteforce_iter(password, alphabet, length); */
  bruteforce_rec(password, alphabet, length, 0);

  return 0;
}
