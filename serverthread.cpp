#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/time.h>

/* You will to add includes here */
#include <string.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <regex.h>
#include <pthread.h>


using namespace std;

static int uid = 10;

void printIpAddr(struct sockaddr_in addr)
{
  printf("Client connected from %d.%d.%d.%d:%d\n",
         addr.sin_addr.s_addr & 0xff,
         (addr.sin_addr.s_addr & 0xff00) >> 8,
         (addr.sin_addr.s_addr & 0xff0000) >> 16,
         (addr.sin_addr.s_addr & 0xff000000) >> 24,
         addr.sin_port);
}

typedef struct
{
  struct sockaddr_in address;
  int clientSock;
  int uid;
} clientDetails;

typedef struct
{
  clientDetails *array;
  size_t used;
  size_t size;
} Array;

Array clients;

pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

void initArray(Array *arr, size_t initialSize)
{
  arr->array = (clientDetails*)malloc(initialSize * sizeof(clientDetails));
  arr->used = 0;
  arr->size = initialSize;
}

void insertClient(Array *arr, clientDetails client)
{
  pthread_mutex_lock(&clients_mutex);
  if (arr->used == arr->size)
  {
    arr->size += 5;
    arr->array = (clientDetails*)realloc(arr->array, arr->size * sizeof(clientDetails));
  }
  arr->array[arr->used++] = client;

  pthread_mutex_unlock(&clients_mutex);
}

void removeClient(Array *arr, int uid)
{
  pthread_mutex_lock(&clients_mutex);

  printf("Removing client\n");
  for (int i = 0; i < (int)arr->used; i++)
  {
    if (arr->array[i].uid == uid)
    {
      for (int j = i; j < ((int)arr->used - 1); j++)
      {
        arr->array[j] = arr->array[j + 1];
      }
      arr->used--;
      break;
    }
  }

  pthread_mutex_unlock(&clients_mutex);
}

void freeArray(Array *arr)
{
  pthread_mutex_lock(&clients_mutex);
  free(arr->array);
  arr->array = NULL;
  arr->used = arr->size = 0;
  pthread_mutex_unlock(&clients_mutex);
}

void *handle_client(void *arg)
{
  int leave_flag = 0;

  clientDetails *currentClient = (clientDetails *)arg;

  insertClient(&clients, *currentClient);
  
  //Recieve a prompt and determine how it should be handled by the threads.
  char prompt[150];
  memset(prompt, 0, 150);
  char fileName[20];
  memset(fileName, 0, 20);
  if (recv(currentClient->clientSock, &prompt, sizeof(prompt), 0) == -1)
  {
    printf("Recieve failed!\n");
  }
  else
  {
    if(prompt[0] == 'A')
    {
      leave_flag = 1;
    }
    else
    {
      int numOfSlashes = 0;
      int startPos = 0;
      for(int i = 0; i < (int)strlen(prompt); i++)
      {
        if(prompt[i] == '/')
        {
          numOfSlashes++;
          if(numOfSlashes == 1)
          {
            startPos = i + 1;
          }
          if(numOfSlashes > 1)
          {
            //Too many slashes
            leave_flag = 1;
            break;
          }
        }
        else if(numOfSlashes == 1 && prompt[i] != '/')
        {
          if(prompt[i] == ' ' && prompt[i + 1] == 'H')
          {
            //HTTP part found.
            break;
          }
          fileName[i - startPos] = prompt[i];
        }
      }
    }
  }

  printf("%s", prompt);
  printf("%s\n", fileName);

  /* Loop if any back and forth is needed
  while (1)
  {
    if (leave_flag)
    {
      break;
    }
  }
  */
  close(currentClient->clientSock);
  removeClient(&clients, currentClient->uid);
  free(currentClient);
  pthread_detach(pthread_self());

  return NULL;
}

int main(int argc, char *argv[])
{

  /* Do more magic */
  if (argc != 2)
  {
    printf("Usage: %s <ip>:<port> \n", argv[0]);
    exit(1);
  }
  /*
    Read first input, assumes <ip>:<port> syntax, convert into one string (Desthost) and one integer (port). 
     Atm, works only on dotted notation, i.e. IPv4 and DNS. IPv6 does not work if its using ':'. 
  */
  char delim[] = ":";
  char *Desthost = strtok(argv[1], delim);
  char *Destport = strtok(NULL, delim);
  if (Desthost == NULL || Destport == NULL)
  {
    printf("Usage: %s <ip>:<port> \n", argv[0]);
    exit(1);
  }
  // *Desthost now points to a sting holding whatever came before the delimiter, ':'.
  // *Dstport points to whatever string came after the delimiter.

  /* Do magic */
  int port = atoi(Destport);

  int backLogSize = 10;
  int yes = 1;

  struct addrinfo hint, *servinfo, *p;
  int rv;
  int serverSock;
  pthread_t tid;

  memset(&hint, 0, sizeof(hint));
  hint.ai_family = AF_UNSPEC;
  hint.ai_socktype = SOCK_STREAM;

  if ((rv = getaddrinfo(Desthost, Destport, &hint, &servinfo)) != 0)
  {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
    return 1;
  }
  for (p = servinfo; p != NULL; p = p->ai_next)
  {
    if ((serverSock = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
    {
      printf("Socket creation failed.\n");
      continue;
    }

    if (setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1)
    {
      perror("setsockopt failed!\n");
      exit(1);
    }

    rv = bind(serverSock, p->ai_addr, p->ai_addrlen);
    if (rv == -1)
    {
      perror("Bind failed!\n");
      close(serverSock);
      continue;
    }
    break;
  }

  freeaddrinfo(servinfo);

  if (p == NULL)
  {
    fprintf(stderr, "Server failed to create an apporpriate socket.\n");
    exit(1);
  }

  printf("[x]Listening on %s:%d \n", Desthost, port);

  rv = listen(serverSock, backLogSize);
  if (rv == -1)
  {
    perror("Listen failed!\n");
    exit(1);
  }

  struct sockaddr_in clientAddr;
  socklen_t client_size = sizeof(clientAddr);

  initArray(&clients, 5);

  int clientSock = 0;

  while (1)
  {
    clientSock = accept(serverSock, (struct sockaddr *)&clientAddr, &client_size);
    if (clientSock == -1)
    {
      perror("Accept failed!\n");
    }

    printIpAddr(clientAddr);

    clientDetails *currentClient = (clientDetails *)malloc(sizeof(clientDetails));
    memset(currentClient, 0, sizeof(clientDetails));
    currentClient->address = clientAddr;
    currentClient->clientSock = clientSock;
    currentClient->uid = uid++;

    pthread_create(&tid, NULL, &handle_client, (void *)currentClient);
  }

  close(serverSock);
  freeArray(&clients);
  printf("done.\n");
  return(0);
}
