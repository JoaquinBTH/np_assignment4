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
#include <sys/wait.h>

using namespace std;

static int uid = 10;

char addition[100];

void printIpAddr(struct addrinfo *addr, struct sockaddr_in port, char *addition)
{
  char ipString[40];
  memset(ipString, 0, 40);
  switch (addr->ai_family)
  {
  case AF_INET:
    inet_ntop(AF_INET, &(((struct sockaddr_in *)addr->ai_addr)->sin_addr), ipString, addr->ai_addrlen);
    printf("%s:%d%s\n", ipString, port.sin_port, addition);
    break;

  case AF_INET6:
    inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)addr->ai_addr)->sin6_addr), ipString, addr->ai_addrlen);
    printf("%s:%d%s\n", ipString, port.sin_port, addition);
    break;
  }
}

typedef struct
{
  struct addrinfo *address;
  struct sockaddr_in port;
  int clientSock;
  int uid;
} clientDetails;

void handleFile(clientDetails *currentClient, char *fileName, char *prompt)
{
  // Find HTTP version
  char http[10];
  memset(http, 0, 10);
  for (int i = 0; i < 10; i++)
  {
    if (prompt[6 + (int)strlen(fileName) + i] != '\r')
    {
      http[i] = prompt[6 + (int)strlen(fileName) + i];
    }
  }

  FILE *currentFile = fopen(fileName, "r");
  if (currentFile != NULL)
  {
    // Print OK
    memset(addition, 0, 100);
    sprintf(addition, " [200]: OK /%s", fileName);
    printIpAddr(currentClient->address, currentClient->port, addition);
    // printf(" [200]: OK /%s\n", fileName);

    // Send 200 OK HTTP protocol to client
    char buf[(int)strlen(http) + 11];
    memset(buf, 0, sizeof(buf));
    sprintf(buf, "%s 200 OK\r\n\r\n", http);
    if (write(currentClient->clientSock, buf, sizeof(buf)) == -1)
    {
      printf("Error sending OK\n");
    }

    // Determine size of the file
    size_t sizeOfFile;
    if (fseek(currentFile, 0, SEEK_END) == -1)
    {
      printf("Error doing fseek, SEEK_END\n");
    }
    sizeOfFile = ftell(currentFile);
    if (fseek(currentFile, 0, SEEK_SET) == -1)
    {
      printf("Error doing fseek, SEEK_SET\n");
    }

    // Separate the writes in increments of 1500 maximum byte size to account for clients not having large enough buffers to contain all the information received.

    if (sizeOfFile > (size_t)1500)
    {
      for (int i = 0; i < (int)sizeOfFile / 1500; i++)
      {
        // Dynamically allocate the required buffer size
        char text[1500];
        memset(text, 0, 1500);

        // Fill up the buffer with the data from the file
        fread(text, sizeof(char), sizeof(text), currentFile);

        // Send the answer over to the client
        if (write(currentClient->clientSock, text, sizeof(text)) == -1)
        {
          printf("Error sending text back to client!\n");
        }
      }
      int remainingSize = sizeOfFile % 1500;
      if (remainingSize > 0)
      {
        // Dynamically allocate the required buffer size
        char text[remainingSize];
        memset(text, 0, remainingSize);

        // Fill up the buffer with the data from the file
        fread(text, sizeof(char), sizeof(text), currentFile);

        // Send the answer over to the client
        if (write(currentClient->clientSock, text, sizeof(text)) == -1)
        {
          printf("Error sending text back to client!\n");
        }
      }
    }
    else
    {
      // Dynamically allocate the required buffer size
      char text[sizeOfFile];
      memset(text, 0, sizeOfFile);

      // Fill up the buffer with the data from the file
      fread(text, sizeof(char), sizeOfFile, currentFile);

      // Send the answer over to the client
      if (write(currentClient->clientSock, text, sizeof(text)) == -1)
      {
        printf("Error sending text back to client!\n");
      }
    }
    // Close the file when we are done using it
    fclose(currentFile);
  }
  else
  {
    // Print Not Found
    memset(addition, 0, 100);
    sprintf(addition, " [404]: Not Found /%s", fileName);
    printIpAddr(currentClient->address, currentClient->port, addition);
    // printf(" [404]: Not Found /%s\n", fileName);

    // Send Error 404 HTTP protocol to client
    char buf[(int)strlen(http) + 18];
    memset(buf, 0, sizeof(buf));
    sprintf(buf, "%s 404 Not Found\r\n\r\n", http);
    if (write(currentClient->clientSock, buf, sizeof(buf)) == -1)
    {
      printf("Error sending Not Found\n");
    }
  }
}

void *handle_client(void *arg)
{
  int leave_flag = 0;

  clientDetails *currentClient = (clientDetails *)arg;

  // Recieve a prompt and check if it's a valid HTTP protocol and meets the requirements
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
    // Check if the first part of the HTTP protocol is GET and then check if there are too many directories required to reach the file
    if (prompt[0] != 'G')
    {
      leave_flag = 1;
    }
    else
    {
      int numOfSlashes = 0;
      int startPos = 0;
      for (int i = 0; i < (int)strlen(prompt); i++)
      {
        if (prompt[i] == '/')
        {
          numOfSlashes++;
          if (numOfSlashes == 1)
          {
            startPos = i + 1;
          }
          if (numOfSlashes > 1)
          {
            // Too many slashes == Too many directories
            leave_flag = 1;
            break;
          }
        }
        else if (numOfSlashes == 1 && prompt[i] != '/')
        {
          if (prompt[i] == ' ' && prompt[i + 1] == 'H')
          {
            // End of the file name reached
            break;
          }
          fileName[i - startPos] = prompt[i];
        }
      }
    }
  }

  // If no errors in Protocol, handle the client request
  if (leave_flag != 1)
  {
    handleFile(currentClient, fileName, prompt);
  }
  else
  {
    // Print Unknown protocol
    memset(addition, 0, 100);
    sprintf(addition, " [400]: Unknown Protocol %s", fileName);
    printIpAddr(currentClient->address, currentClient->port, addition);
    // printf(" [400]: Unknown Protocol %s\n", fileName);

    // Send Error 400 HTTP protocol to client
    char buf[34] = "HTTP/x.x 400 Unknown Protocol\r\n\r\n";
    if (write(currentClient->clientSock, buf, sizeof(buf)) == -1)
    {
      printf("Error sending Unknown Protocol\n");
    }
  }

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
  */
  char *Desthost;
  char *Destport;

  int IPv6 = 0;

  // IPv4
  char delim[] = ":";
  Desthost = strtok(argv[1], delim);
  Destport = strtok(NULL, delim);

  // Parse if DNS to see if it's IPv4 or IPv6
  struct addrinfo hint, *servinfo, *p;
  int rv;
  memset(&hint, 0, sizeof(hint));
  hint.ai_family = AF_UNSPEC;
  hint.ai_socktype = SOCK_STREAM;

  if ((rv = getaddrinfo(Desthost, Destport, &hint, &servinfo)) != 0)
  {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
    return 1;
  }

  switch (servinfo->ai_family)
  {
  case AF_INET:
    IPv6 = 0;
    break;
  case AF_INET6:
    IPv6 = 1;
    break;
  }

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

  int serverSock;
  pid_t fid;

  if (IPv6)
  {
    struct sockaddr_in6 ipv6Addr;
    memset(&ipv6Addr, 0, sizeof(ipv6Addr));
    ipv6Addr.sin6_family = AF_INET6;
    ipv6Addr.sin6_port = htons(port);
    ipv6Addr.sin6_addr = in6addr_any;

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

      rv = bind(serverSock, (struct sockaddr *)&ipv6Addr, sizeof(ipv6Addr));
      if (rv == -1)
      {
        perror("Bind failed!\n");
        close(serverSock);
        continue;
      }
      break;
    }
  }
  else
  {
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
  }

  if (p == NULL)
  {
    fprintf(stderr, "Server failed to create an apporpriate socket.\n");
    exit(1);
  }

  printf("[x]Forked Server Listening on %s:%d\r\n\r\n", Desthost, port);

  rv = listen(serverSock, backLogSize);
  if (rv == -1)
  {
    perror("Listen failed!\n");
    exit(1);
  }

  struct sockaddr_in clientAddr;
  socklen_t client_size = sizeof(clientAddr);

  int clientSock = 0;

  signal(SIGPIPE, SIG_IGN);

  while (1)
  {
    clientSock = accept(serverSock, (struct sockaddr *)&clientAddr, &client_size);
    if (clientSock == -1)
    {
      perror("Accept failed!\n");
    }

    clientDetails *currentClient = (clientDetails *)malloc(sizeof(clientDetails));
    memset(currentClient, 0, sizeof(clientDetails));
    currentClient->address = servinfo;
    currentClient->port = clientAddr;
    currentClient->clientSock = clientSock;
    currentClient->uid = uid++;

    memset(addition, 0, 100);
    sprintf(addition, " Accepted");
    printIpAddr(currentClient->address, currentClient->port, addition);

    for (int i = 0; i < 3; i++)
    {
      fid = fork();
      if (fid == -1)
      {
        printf("Fork failed\n");
        sleep(1);
      }
      else
      {
        break;
      }
    }

    if (fid == 0)
    {
      // Child process
      handle_client((void *)currentClient);
      // Close the socket
      memset(addition, 0, 100);
      sprintf(addition, " Closing");
      printIpAddr(currentClient->address, currentClient->port, addition);
      close(currentClient->clientSock);
      free(currentClient);
      exit(0);
    }
    fid = wait(NULL);
    close(currentClient->clientSock);
    free(currentClient);
  }

  freeaddrinfo(servinfo);
  close(serverSock);
  printf("done.\n");
  return (0);
}
