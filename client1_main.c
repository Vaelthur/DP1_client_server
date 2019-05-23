/*
 * TEMPLATE 
 */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>

#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../sockwrap.h"
#include "../errlib.h"

#define MAXBUF 512
#define LISTENQ 15

char *prog_name;

void sigpipe_handler(int sig)
{
    printf("sigterm\n");
    return;
}

int main (int argc, char *argv[])
{
	//check su # argomenti: dovrebbero essere serv_addr , serv_port , file da trasferire
	if(argc < 4) { 
		err_quit("\n # Wrong usage. Correct usage: './<prog_name> <server address> <server port> <file to transfer> [<file to transfer>]' #\n");
	}

	prog_name=argv[0];

	//strutture dati necessarie
	int s, nread, nsent, i, cntf=3; //i indice per ciclo se #file > 1; cntf counter per file per identificarli.
	uint32_t size_file, timestamp;
    struct addrinfo hints, *res;
	char *namefile;
	char sendbuf[MAXBUF], recbuf[MAXBUF], recbuf2[MAXBUF];

	//strutture per timer
	struct timeval timeout;
	timeout.tv_sec = LISTENQ;
	timeout.tv_usec = 0;

	struct sigaction sa;
    sa.sa_handler = sigpipe_handler;
    sa.sa_flags = 0; // or SA_RESTART
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGPIPE, &sa, NULL) == -1) {
        perror("sigpipe error");
        exit(1);
    }

    memset(&hints, 0, sizeof(hints));	// svuota struct
    hints.ai_family = AF_INET; 			
    hints.ai_socktype = SOCK_STREAM;	// client TCP

    Getaddrinfo(argv[1], argv[2], &hints, &res);
    s = Socket(res->ai_family, res->ai_socktype, res->ai_protocol);

    Connect(s,res->ai_addr,res->ai_addrlen);

	//strutture dati & codice per ricevere contenuto file.
	int tot=0, remain_data;
	FILE *fd;

	// ciclo per poter iterare su più file
	for(i=0; i<(argc-3); i++, cntf++) {

		if(setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout,sizeof(timeout)) < 0){
       		printf("Time Out\n");
      		return -1;
    	}

		namefile=argv[cntf];

		memset(recbuf2, 0, MAXBUF);
		memset(recbuf, 0, MAXBUF);
		memset(sendbuf, 0, MAXBUF);
		//invio caratteri per richiedere il file + check
		sprintf(sendbuf, "GET %s\r\n", namefile);
		nsent = send(s, sendbuf, strlen(sendbuf), 0);

		if(nsent < 0)
			err_quit("(%s) - Error: send() failed\n", prog_name);
		if(nsent == 0)
			err_quit("(%s) - Server performed shutdown\n", prog_name);
		
		//apro file corrente
		fd = fopen(argv[cntf], "wb");
		if (fd == NULL) {
			err_quit("(%s) - Error: fopen() failed\n", prog_name);
		}

		
		//ricevo l'OK o eventuale ERR
		nread = readline_unbuffered(s, recbuf2, MAXBUF);

		if (strncmp(recbuf2, "-ERR\r\n", 6) == 0) {
			err_quit("(%s) - Error: file not found\n", prog_name);
			remove(argv[cntf]);
		} 
		if (strncmp(recbuf2, "+OK\r\n", 5) != 0) {
			err_quit("(%s) - Error in protocol messages\n", prog_name);
		}
		if(nread == 0) {
			Fclose(fd);
			remove(argv[cntf]);
			err_quit("(%s) - Server performed shutdown\n", prog_name);
		}

		//resetto buffer + ricevo la lunghezza del contenuto
		memset(recbuf2, 0, MAXBUF);
		nread = recv(s, recbuf2, 4, 0);
		if(nread == 0) {
			Fclose(fd);
			remove(argv[cntf]);
			err_quit("(%s) - Server performed shutdown\n", prog_name);
		}

		//output a video + conversione
		printf("file = %s\n", argv[cntf]);
		memcpy(&size_file, recbuf2, 4);
		size_file = ntohl(size_file);
		printf("size = %u\n", size_file);

		//ricevo i dati veri e propri
		tot=0;
		remain_data = size_file;
		while(tot < size_file) {
			if(remain_data > MAXBUF) 
				nread = Recv(s, recbuf, MAXBUF, 0);
			else {
				nread = Recv(s, recbuf, remain_data, 0);
			}
			if (nread == 0) {
				Fclose(fd);
				remove(argv[cntf]);
				err_quit("(%s) - Server performed shutdown\n", prog_name);
			}
			tot += nread;
			remain_data -= nread;
			fwrite(recbuf, sizeof(char), nread, fd);
		}
		Fclose(fd);	

		//ricevo timestamp
		memset(recbuf2, 0, MAXBUF);
		nread = 0;
		nread = recv(s, recbuf2, 4, 0);
		memcpy(&timestamp, recbuf2, 4);
		timestamp = ntohl(timestamp);
		printf("timestamp = %u\n\n", timestamp);
	}

	//chiusura connessione
	shutdown(s, SHUT_WR); //non devo più inviare nulla 
	freeaddrinfo(res); 
	Close(s);
	return 0;
}