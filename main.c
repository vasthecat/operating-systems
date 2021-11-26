#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <crypt.h>
#include <pthread.h>
#include <semaphore.h>

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
  char *hash;
};

struct task_t
{
  char password[8];
};

struct queue_t
{
  struct task_t tasks[8];
  int size, capacity;
  int head, tail;
  pthread_mutex_t head_mut, tail_mut;
  sem_t empty, full;
};

void
queue_init(struct queue_t *queue)
{
  queue->size = 0;
  queue->capacity = 8;
  queue->head = 0;
  queue->tail = queue->capacity - 1;
  sem_init(&queue->full, 0, queue->capacity);
}

void
queue_push(struct queue_t *queue, struct task_t *task)
{
  sem_wait(&queue->full);
  
  pthread_mutex_lock(&queue->head_mut);
  queue->size++;
  queue->tail = (queue->tail + 1) % queue->capacity;
  queue->tasks[queue->tail] = *task;
  pthread_mutex_unlock(&queue->head_mut);

  printf("Enqued '%s'\n", task->password);
}

void
queue_pop(struct queue_t *queue, struct task_t *task)
{
  sem_post(&queue->full);

  pthread_mutex_lock(&queue->tail_mut);
  queue->size--;
  *task = queue->tasks[queue->head];
  queue->head = (queue->head + 1) % queue->capacity;
  pthread_mutex_unlock(&queue->tail_mut);

  printf("Dequed '%s'\n", task->password);
}

bool
check_password(char *password, char *hash)
{
  char *hashed = crypt(password, hash);
  return strcmp(hashed, hash) == 0;
}

bool
bruteforce_rec(char *password, struct config_t *config, int pos)
{
  if (config->length == pos)
  {
    if (check_password(password, config->hash))
      return true;
  }
  else
  {
    for (int i = 0; config->alphabet[i] != '\0'; ++i)
    {
      password[pos] = config->alphabet[i];
      if (bruteforce_rec(password, config, pos + 1))
	return true;
    }
  }
  return false;
}

bool
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

    if (check_password(password, config->hash))
      return true;
  }
  return false;
}

void
parse_opts(struct config_t *config, int argc, char *argv[])
{
  int opt;
  opterr = 1;
  while ((opt = getopt(argc, argv, "ira:l:h:")) != -1)
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
    case 'h':
      config->hash = optarg;
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
    .mode = M_ITERATIVE,
    .hash = "hiN3t5mIZ/ytk", // hi + abcd
  };
  parse_opts(&config, argc, argv);

  struct queue_t queue;
  queue_init(&queue);
  struct task_t task, result;
  task = (struct task_t) { .password = "1234567" };
  queue_push(&queue, &task);
  task = (struct task_t) { .password = "hello" };
  queue_push(&queue, &task);
  task = (struct task_t) { .password = "there" };
  queue_push(&queue, &task);

  queue_pop(&queue, &result);
  queue_pop(&queue, &result);
  queue_pop(&queue, &result);

  /* char password[config.length + 1]; */
  /* password[config.length] = '\0'; */

  /* bool found = false; */
  /* switch (config.mode) */
  /* { */
  /* case M_ITERATIVE: */
  /*   found = bruteforce_iter(password, &config); */
  /*   break; */
  /* case M_RECURSIVE: */
  /*   found = bruteforce_rec(password, &config, 0); */
  /*   break; */
  /* } */

  /* if (found) */
  /* { */
  /*   printf("Found password: '%s'\n", password); */
  /* } */
  /* else */
  /* { */
  /*   printf("Password not found\n"); */
  /* } */

  return 0;
}
