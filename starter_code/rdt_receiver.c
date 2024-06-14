#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <assert.h>
#include <errno.h>
#include <signal.h>


#include "common.h"
#include "packet.h"

tcp_packet *recvpkt;
tcp_packet *sndpkt;
int counter = 0;
struct timeval start_time; // Global variable to store start time

void timeout_handler(int signum) {
    struct timeval end_time;
    gettimeofday(&end_time, NULL);
    long int elapsed_sec = end_time.tv_sec - start_time.tv_sec;
    printf("No more files received, closing connection! Elapsed time: %ld seconds (%ld)\n", elapsed_sec, start_time.tv_sec);
    exit(0);
}

void start_timer(int seconds) {
    struct itimerval timer;
    signal(SIGALRM, timeout_handler);
    timer.it_value.tv_sec = seconds;
    timer.it_value.tv_usec = 0;
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = 0;
    setitimer(ITIMER_REAL, &timer, NULL);
    gettimeofday(&start_time, NULL); // Store the start time
}

void stop_timer() {
    struct itimerval timer;
    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = 0;
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = 0;
    setitimer(ITIMER_REAL, &timer, NULL);
}

int main(int argc, char **argv) {
    int sockfd;
    int portno;
    int clientlen;
    struct sockaddr_in serveraddr;
    struct sockaddr_in clientaddr;
    int optval;
    FILE *fp;
    char buffer[MSS_SIZE];
    struct timeval tp;
    int seqno = 0;

    if (argc != 3) {
        fprintf(stderr, "usage: %s <port> FILE_RECVD\n", argv[0]);
        exit(1);
    }
    portno = atoi(argv[1]);

    fp = fopen(argv[2], "w");
    if (fp == NULL) {
        error(argv[2]);
    }

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
        error("ERROR opening socket");

    optval = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval, sizeof(int));

    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons((unsigned short)portno);

    if (bind(sockfd, (struct sockaddr *) &serveraddr, sizeof(serveraddr)) < 0)
        error("ERROR on binding");

    VLOG(DEBUG, "epoch time, bytes received, sequence number");

    clientlen = sizeof(clientaddr);
    int eof_received = 0;
while (1) {
    if(recvfrom(sockfd, buffer, MSS_SIZE, 0, (struct sockaddr *) &clientaddr, (socklen_t *)&clientlen) < 0){
	error("ERROR in recvfrom");	
    }
    recvpkt = (tcp_packet *) buffer;
    assert(get_data_size(recvpkt) <= DATA_SIZE);

    if (recvpkt->hdr.data_size == 0) {
	counter = 1;
        VLOG(INFO, "End Of File has been reached");
        sndpkt = make_packet(0);
        sndpkt->hdr.ackno = recvpkt->hdr.seqno + 1;
        sndpkt->hdr.ctr_flags = ACK;
	//printf("%d\n", sndpkt->hdr.ackno);
        if (sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0, (struct sockaddr *) &clientaddr, clientlen) < 0) {
            error("ERROR in sendto");
        }
	if (!eof_received) {
		printf("Starting 20 second timeout time for TCP closure!\n");
                start_timer(20); //start a 10 second timer
                eof_received = 1;
    	}
    }

    if (recvpkt->hdr.seqno == seqno) {
 	if(eof_received){
		stop_timer();
		eof_received = 0;
		start_timer(20);	
	}
	gettimeofday(&tp, NULL);
	VLOG(DEBUG, "%lu, %d, %d", tp.tv_sec, recvpkt->hdr.data_size, recvpkt->hdr.seqno);
        fseek(fp, recvpkt->hdr.seqno, SEEK_SET);
        fwrite(recvpkt->data, 1, recvpkt->hdr.data_size, fp);
        seqno += recvpkt->hdr.data_size;
        sndpkt = make_packet(0);
        sndpkt->hdr.ackno = recvpkt->hdr.seqno + recvpkt->hdr.data_size;
        sndpkt->hdr.ctr_flags = ACK;
        if (sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0, (struct sockaddr *) &clientaddr, clientlen) < 0) {
            error("ERROR in sendto");
        }

    } else {
        sndpkt = make_packet(0);
        sndpkt->hdr.ackno = seqno;
        sndpkt->hdr.ctr_flags = ACK;
        if (sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0, (struct sockaddr *) &clientaddr, clientlen) < 0) {
            error("ERROR in sendto");
        }
    }
}
    fclose(fp);

    return 0;
}