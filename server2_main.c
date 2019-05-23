#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/sendfile.h>
#include <sys/select.h>

#include <rpc/xdr.h>

#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../sockwrap.h"
#include "../errlib.h"

#define LISTENQ 15
#define MAXBUF 511



char *prog_name;

int main (int argc, char *argv[]) {

    //check sui parametri passati da linea di comando
	if(argc != 2) {
		err_quit("\n# Wrong usage. Please run this server is this way: './<prog_name> <port_number>' #\n");
	}

	prog_name=argv[0];
	
	//strutture dati necessarie
	uint16_t port = atoi(argv[1]); //porta server
	struct sockaddr_in saddr, caddr;
	socklen_t addrlen = sizeof(caddr);
	int s, s1, cntf=0, nread, nsent, childpid;
	char rbuf[MAXBUF+1], errmsg[] = "-ERR\r\n", sendmsg[] = "+OK\r\n", sbuf[MAXBUF], sbuf2[MAXBUF];
	char nomefile[MAXBUF];
    //struct necessarie per timer
	struct timeval timeout;
	timeout.tv_sec = LISTENQ;
    timeout.tv_usec = 0;

	s = Socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	
	memset(&saddr, 0, sizeof(saddr));
	saddr.sin_family = AF_INET;
	saddr.sin_port = htons(port);
	saddr.sin_addr.s_addr = htonl(INADDR_ANY);

	
	//binding per assegnazione indirizzi
	Bind(s, (struct sockaddr *) &saddr, addrlen);

	
	printf("(%s) - Socket created\n", prog_name);
	printf("(%s) - Listening on %s:%u\n", prog_name, inet_ntoa(saddr.sin_addr), ntohs(saddr.sin_port));

	Listen(s, LISTENQ);

    while(1) {
        printf("(%s) - Waiting for connections...\n", prog_name);
		cntf = 0;
		int retry = 0;
		
		do {
			s1 = Accept(s, (struct sockaddr *) &caddr, &addrlen);
			printf("(%s) - New connection from client %s:%u\n", prog_name, inet_ntoa(caddr.sin_addr), ntohs(caddr.sin_port));
			retry = 0;
		} while (retry);

        //eseguo fork del processo per concorrenza
        if((childpid = fork()) < 0)
            err_quit("(%s) - Error: fork() failed\n");
        else if(childpid > 0) { //processo padre
            close(s1); //chiudo socket figlio
            
        } else {//processo figlio, childpid == 0
            close(s); //chiudo socket padre;

            while(1) {
                //opzione per il timeout
		    	if(setsockopt(s1, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout,sizeof(timeout)) < 0){
        			printf("Time Out\n");
      			return -1;
    	    	}

			    //reset dati
			    nread=0;
			    memset(rbuf, 0, sizeof(rbuf));
			    memset(sbuf2, 0, sizeof(sbuf2));
			    memset(sbuf, 0, sizeof(sbuf));

                //leggo nome del file secondo protocollo
                nread = readline_unbuffered(s1, rbuf, MAXBUF);

                if(nread == 0) {
                    if(cntf == 1) 
                        printf("(%s) - Completed file transfer\n", prog_name);
                    else
                        printf("(%s) - Completed files transfers\n", prog_name);
                    break;
                } else if (nread < 0) {
                    err_quit("(%s) - Error: readline() failed\n", prog_name);
                }

                cntf++;

                //check che il protocollo sia rispettato
                if(strncmp(rbuf, "GET ", 4) != 0) {
                    send(s1, errmsg, sizeof(errmsg), 0); 
                    err_quit("(%s) - Error in protocol messages\n", prog_name);
                }

                //ricavo il nome del file
                sscanf(rbuf, "GET %s\r\n", nomefile);

                //apro il file richiesto
                // + strutture dati necessarie per invio file
                FILE *filedescr = fopen(nomefile, "rb"); //metto path quando lo fo girare sul mio
                struct stat st;
                int remain_data, tot;
                uint32_t file_size, timestamp;

                //check apertura file
                if (filedescr == NULL)  {
                    send(s1, errmsg, 6, 0);
                    err_quit("(%s) - Error: file not found\n", prog_name);
                }
                
                //invio messaggio con protocollo
                nsent = send(s1, sendmsg, sizeof(sendmsg)-1, 0);
                //check
			    if(nsent == 0) {
			    	err_quit("(%s) - Error: client could not be reached\n", prog_name);
			    }
			    if(nsent < 0) {
			    	err_quit("(%s) - Error: send() failed\n", prog_name);
			    }

                //ricavo stat del file
                if(stat(nomefile, &st) == 0) {
                    file_size = st.st_size;
                } else {
                    send(s1, errmsg, sizeof(errmsg), 0);
                    err_quit("(%s) - Error during file stat\n", prog_name);
                }
                
                //invio dimensione file; TODO check su nread
                file_size=htonl(st.st_size);
                memcpy(sbuf2, &file_size, 4);
                nsent = send(s1, sbuf2, sizeof(uint32_t), 0);
                //check
			    if(nsent == 0) {
			    	err_quit("(%s) - Error: client could not be reached\n", prog_name);
			    }
			    if(nsent < 0) {
			    	err_quit("(%s) - Error: send() failed\n", prog_name);
			    }

                //lettura file & invio byte
                remain_data = st.st_size;
                tot =0;
                while(tot < st.st_size){
                    if(remain_data > MAXBUF) {
                        nread = fread(sbuf, 1, MAXBUF, filedescr);
                    } else {
                        nread = fread(sbuf, 1, remain_data, filedescr);
                    }
                    tot += nread;
                    remain_data -= nread;
                    send(s1, sbuf, nread, 0);
                }                    

                memset(sbuf2,0, sizeof(sbuf2));
                timestamp = htonl(st.st_mtime);
                memcpy(sbuf2, &timestamp, 4);
                nsent = send(s1, sbuf2, sizeof(uint32_t), 0);

                if(nsent < 0) {
				    err_quit("(%s) - Error: send() failed\n", prog_name);
		    	}
		    	if(nsent == 0) {
				    printf("(%s) - Error: client is offline\n", prog_name);
				    break;
		    	}

                Fclose(filedescr);

                printf("(%s) - File '%s' sent & closed.\n", prog_name, nomefile);
		    }
		close(s1);
        exit(0);
        }
    }
    return 0;
}