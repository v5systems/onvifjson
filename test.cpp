#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>


#define ONVIF_PROT_MARKER       0x5432fbca
#define BUFSIZE                 65536
#define LISTENIP                "127.0.0.1"
#define LISTENPORT              4461

using namespace std;

struct tHeader{
  uint32_t marker;
  uint32_t mesID;
  uint32_t dataLen;
  unsigned char data;
};

typedef tHeader* pHeader;
const size_t sHeader = 12;


int main(int argc, char **argv) {
    int sockfd, portno, n;
    struct sockaddr_in serveraddr;
    struct hostent *server;
    char *hostname;
    char *command;
    char buf[BUFSIZE];

    /* check command line arguments */
    if (argc != 4) {
       fprintf(stderr,"usage: %s <hostname> <port> <command>\n", argv[0]);
       exit(0);
    }
    hostname = argv[1];
    portno = atoi(argv[2]);
    command = argv[3];

  //
    struct sockaddr_in cli_addr;

    bzero((char *) &cli_addr, sizeof(cli_addr));

    cli_addr.sin_family = AF_INET;
    cli_addr.sin_port = htons(portno);
    cli_addr.sin_addr.s_addr = inet_addr(hostname);

    int clisockfd = socket(AF_INET , SOCK_STREAM , 0);
    if (clisockfd  < 0){
        perror("ERROR opening client socket");
        return -9;
    }


    if (connect(clisockfd, (struct sockaddr *)&cli_addr, sizeof(cli_addr))){
        perror("ERROR connecting client socket");
        return -10;
    }


    pHeader tmpHeader= (pHeader)buf;
    tmpHeader->dataLen=strlen(command);
    tmpHeader->mesID=55555;
    tmpHeader->marker=ONVIF_PROT_MARKER;
    memcpy(buf+sHeader,command,strlen(command));

    /* send the message line to the server */
    n = write(clisockfd, buf, strlen(command) + sHeader);
    if (n < 0){
      perror("ERROR writing to client socket");
      close(clisockfd);
      return -11;
    }

    /* print the server's reply */
    bzero(buf, BUFSIZE);
    n = read(clisockfd, buf, BUFSIZE);
    if (n < 0){
      perror("ERROR reading from client socket");
      close(clisockfd);
      return -12;
    }

    buf[n] = 0;
    printf("Response: \n%s\n", buf+sHeader);

    close(clisockfd);
    return 0;
}
