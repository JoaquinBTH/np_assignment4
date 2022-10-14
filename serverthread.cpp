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
  printf("%d.%d.%d.%d:%d",
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

void handleFile(clientDetails* currentClient, char* fileName, char* prompt)
{
  //Find HTTP version
  char http[10];
  memset(http, 0, 10);
  for(int i = 0; i < 10; i++)
  {
    if(prompt[6 + (int)strlen(fileName) + i] != '\r')
    {
      http[i] = prompt[6 + (int)strlen(fileName) + i];
    }
  }

  FILE* currentFile = fopen(fileName, "r");
  if(currentFile != NULL)
  {
    //Print OK
    printIpAddr(currentClient->address);
    printf(" [200]: OK /%s\n", fileName);

    //Send 200 OK HTTP protocol to client
    char buf[(int)strlen(http) + 11];
    memset(buf, 0, sizeof(buf));
    sprintf(buf, "%s 200 OK\r\n\r\n", http);
    if(write(currentClient->clientSock, buf, sizeof(buf)) == -1)
    {
      printf("Error sending OK\n");
    }

    //Determine size of the file
    size_t sizeOfFile;
    if(fseek(currentFile, 0, SEEK_END) == -1)
    {
      printf("Error doing fseek, SEEK_END\n");
    }
    sizeOfFile = ftell(currentFile);
    if(fseek(currentFile, 0, SEEK_SET) == -1)
    {
      printf("Error doing fseek, SEEK_SET\n");
    }

    //TODO: Separate to 1500 bytes maximum and iterate until everything has been sent.
    //Example: If a file is 2000 bytes, send a buffer with 1500 bytes and then another one with the remaining 500.

    //Dynamically allocate the required buffer size
    char text[sizeOfFile];
    memset(text, 0, sizeOfFile);

    //Fill up the buffer with the data from the file
    fread(text, sizeof(char), sizeOfFile, currentFile);

    //Send the answer over to the client
    if(write(currentClient->clientSock, text, sizeof(text)) == -1)
    {
      printf("Error sending text back to client!\n");
    }

    //Close the file when we are done using it
    fclose(currentFile);
  }
  else
  {
    //Print Not Found
    printIpAddr(currentClient->address);
    printf(" [404]: Not Found /%s\n", fileName);

    //Send Error 404 HTTP protocol to client
    char buf[(int)strlen(http) + 18];
    memset(buf, 0, sizeof(buf));
    sprintf(buf, "%s 404 Not Found\r\n\r\n", http);
    if(write(currentClient->clientSock, buf, sizeof(buf)) == -1)
    {
      printf("Error sending Not Found\n");
    }
  }
}

void *handle_client(void *arg)
{
  int leave_flag = 0;

  clientDetails *currentClient = (clientDetails *)arg;
  
  //Recieve a prompt and check if it's a valid HTTP protocol and meets the requirements
  char prompt[150];
  memset(prompt, 0, 150);
  char fileName[50];
  memset(fileName, 0, 50);
  if (recv(currentClient->clientSock, &prompt, sizeof(prompt), 0) == -1)
  {
    printf("Recieve failed!\n");
  }
  else
  {
    //Check if the first part of the HTTP protocol is GET and then check if there are too many directories required to reach the file
    if(prompt[0] != 'G')
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
            //Too many slashes == Too many directories
            leave_flag = 1;
            break;
          }
        }
        else if(numOfSlashes == 1 && prompt[i] != '/')
        {
          if(prompt[i] == ' ' && prompt[i + 1] == 'H')
          {
            //End of the file name reached
            break;
          }
          fileName[i - startPos] = prompt[i];
        }
      }
    }
  }

  //If no errors in Protocol, handle the client request
  if(leave_flag != 1)
  {
    handleFile(currentClient, fileName, prompt);
  }
  else
  {
    //Print Unknown protocol
    printIpAddr(currentClient->address);
    printf(" [400]: Unknown Protocol %s\n", fileName);

    //Send Error 400 HTTP protocol to client
    char buf[34] = "HTTP/x.x 400 Unknown Protocol\r\n\r\n";
    if(write(currentClient->clientSock, buf, sizeof(buf)) == -1)
    {
      printf("Error sending Unknown Protocol\n");
    }
  }

  //Close the socket
  printIpAddr(currentClient->address);
  printf(" Closing\n");

  close(currentClient->clientSock);
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

  printf("[x]Threaded Server Listening on %s:%d\r\n\r\n", Desthost, port);

  rv = listen(serverSock, backLogSize);
  if (rv == -1)
  {
    perror("Listen failed!\n");
    exit(1);
  }

  struct sockaddr_in clientAddr;
  socklen_t client_size = sizeof(clientAddr);

  int clientSock = 0;

  while (1)
  {
    clientSock = accept(serverSock, (struct sockaddr *)&clientAddr, &client_size);
    if (clientSock == -1)
    {
      perror("Accept failed!\n");
    }

    printIpAddr(clientAddr);
    printf(" Accepted\n");

    clientDetails *currentClient = (clientDetails *)malloc(sizeof(clientDetails));
    memset(currentClient, 0, sizeof(clientDetails));
    currentClient->address = clientAddr;
    currentClient->clientSock = clientSock;
    currentClient->uid = uid++;

    pthread_create(&tid, NULL, &handle_client, (void *)currentClient);
  }

  close(serverSock);
  printf("done.\n");
  return(0);
}
