#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/time.h>

#define BUFSIZE 65536

//error wrapper for system calls
void error(char *msg) {
	fprintf(stderr, "Error %s\n", msg);
	exit(-1);
}

//use to store info on server
typedef struct {
	struct hostent *server;
	struct sockaddr_in serveraddr;
	int serverlen;
} serverinfo;

//next two functions make life easier
void uftp_sendto(int sockfd, char *buf, int bufsize, serverinfo *s) {
	if(sendto(sockfd, buf, bufsize, 0, (struct sockaddr*)&s->serveraddr, s->serverlen) < 0)
		error("sending data to server");
}

void uftp_recvfrom(int sockfd, char *buf, serverinfo *s) {
	if(recvfrom(sockfd, buf, BUFSIZE, 0, (struct sockaddr*)&s->serveraddr, &s->serverlen) < 0)
		error("receiving data from server, check IP and port numbers");
}


//prototypes for put and get, and get_file_size used in put
void put(char*, char*, int, serverinfo*);
void get(char*, char*, int, serverinfo*);
int get_file_size(FILE*);
int checkCommand(char*, char*, char**, char**);

int main(int argc, char **argv) {
	int sockfd;
	serverinfo s;
	char buf[BUFSIZE];
	char *command, *filename;
	char parsetmp[BUFSIZE];
	
	if(argc != 3) {
		printf("Usage: %s <server IP> <server port>\n", argv[0]);
		exit(-1);
	}
	
	struct timeval timeout;
	timeout.tv_sec = 1;
	timeout.tv_usec = 0;
	
	//create socket
	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if(sockfd < 0) error("opening socket");
	if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO,&timeout,sizeof(timeout)) < 0)
		error("setting timeout on socket");
	
	//fill in server info
	s.server = gethostbyname(argv[1]);
	if(s.server==NULL) error("in gethostbyname()");
	
	bzero((char *)&s.serveraddr, sizeof(s.serveraddr));
	s.serveraddr.sin_family = AF_INET;
	bcopy((char *)s.server->h_addr, (char *)&s.serveraddr.sin_addr.s_addr, s.server->h_length);
	s.serveraddr.sin_port = htons(atoi(argv[2]));
	
	s.serverlen = sizeof(s.serveraddr);
	
	
	printf("Please enter any of the following commands:\n");
	printf("put [filename]\n");
	printf("get [filename]\n");
	printf("delete [filename]\n");
	printf("ls\n");
	printf("exit\n\n");
	//LOWER PRIORITY, MODULARIZE THIS FOR AN -h COMMAND
	
	while(1) {
		printf(">");
	
		bzero(buf, BUFSIZE);
		fgets(buf, BUFSIZE, stdin);
		
		if(checkCommand(buf, parsetmp, &command, &filename) < 0)
			continue;
			
		uftp_sendto(sockfd, buf, strlen(buf), &s);
			
		bzero(buf, BUFSIZE);

		if(strcmp(command, "put")==0) {
			if(filename==NULL) {
				fprintf(stderr, "Error: you must enter a filename with put\n");
				continue;
			}
			put(filename, buf, sockfd, &s);
		}
		
		else if(strcmp(command, "get")==0) {
			if(filename==NULL) {
				fprintf(stderr, "Error: you must enter a filename with get\n");
				continue;
			}
			get(filename, buf, sockfd, &s);
		}
		
		else if(strcmp(command, "delete")==0) {
			if(filename==NULL)
				fprintf(stderr, "Error: you must enter a filename with delete\n");
			if(strcmp(filename, "server")==0)
				printf("Invalid command\n");
		}
		
		else if(strcmp(command, "ls")==0) {
			if(filename!=NULL) {
				printf("Usage: ls\n");
				continue;
			}
			uftp_recvfrom(sockfd, buf, &s);
			printf("%s\n", buf);
		}
		
		else if(strcmp(command, "exit")==0) {
			if(filename!=NULL) {
				printf("Usage: exit\n");
				continue;
			}
			uftp_recvfrom(sockfd, buf, &s);
			printf("%s\n", buf);
			exit(0);
		} else {
			printf("Please enter a valid command\n");
		}
	}
}

void put(char *filename, char *buf, int sockfd, serverinfo *s) {
	FILE *fp;
	int filesize = -1;
	
	if(strcmp(filename, "server")==0) {
		printf("You cannot access that file\n");
		return;
	}	
	
	fp = fopen(filename, "r");
	if(fp == NULL) {
		printf("Error opening file, please check it exists\n");
		uftp_sendto(sockfd, (char *) &filesize, sizeof(int), s);
		return;
	}
	
	filesize = get_file_size(fp);
	if(filesize > BUFSIZE) {
		printf("File too large to transfer\n");
		uftp_sendto(sockfd, (char*)&filesize, sizeof(int), s);
		return;
	}
	
	uftp_sendto(sockfd, (char*)&filesize, sizeof(int), s);
	
	uftp_recvfrom(sockfd, buf, s);
	if(strcmp(buf, "ACK")!=0) {
		fprintf(stderr, "Couldn't receive ACK\n");
		return;
	}
	
	bzero(buf, BUFSIZE);

	if(fread(buf, 1, filesize, fp) < filesize) {
		fprintf(stderr, "Error: couldn't read entire file");
	} else {
		uftp_sendto(sockfd, buf, filesize, s);
	}
	
	if(fclose(fp) != 0) error("closing file");
}

void get(char *filename, char *buf, int sockfd, serverinfo *s) {
	FILE *fp;
	int filesize;
	
	if(strcmp(filename, "server")==0) {
		printf("You cannot access that file\n");
		return;
	}
	
	if(access(filename, F_OK) != -1) {
		printf("A file with the same name already exists in your current directory\n");
		printf("Overwrite it (y/n)?");
		char c = getchar();
		while(c != 'y' && c!='n') {
			printf("Please enter y or n ");
			c = getchar();
		}
		if(c=='n')
			return;
	}
	
	uftp_recvfrom(sockfd, (char*)&filesize, s);
	if(filesize==-1) {
		printf("Please enter a valid filename (check files with ls)\n");
		return;
	}
	
	fp = fopen(filename, "wb");
	if(fp == NULL) error("opening file for put");
	
	uftp_sendto(sockfd, "ACK", strlen("ACK"), s);
	
	uftp_recvfrom(sockfd, buf, s);
	
	if(fwrite(buf, 1, filesize, fp) < filesize) {
		fprintf(stderr, "Couldn't write full contents of file\n");
	}
	
	if(fclose(fp) != 0) error("closing file");
}

int get_file_size(FILE *fp) {
	int size;
	
	fseek(fp, 0, SEEK_END);
	size = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	
	return size;
}

int checkCommand(char *buf, char *parsetmp, char **command, char **filename) {
	if(strlen(buf)==BUFSIZE-1 && buf[BUFSIZE - 2]!='\n') {
		fprintf(stderr, "Command input too long!\n");
		return -1;
	}
	if(strcmp(buf, "\n")==0) {
		printf("You must enter a command\n");
		return -1;
	}
	
	char *error;
	strcpy(parsetmp, buf);
	
	*command = strtok(parsetmp, " \n\t");
	*filename = strtok(NULL, " \n\t");
	error = strtok(NULL, " \n\t");
	if(error!=NULL) {
		printf("Error: too many commands given\n");
		return -1;
	}
	
	if((*filename)!=NULL) {
		if((*filename)[0]=='.') {
			printf("You may not use pathed filenames; all files\n");
			printf("must be sourced in the executable's current\n");
			printf("working directory or server's cwd\n");
			return -1;
		}
		for(int i=0; i < strlen(*filename); i++) {
			if((*filename)[i]=='/') {
				printf("You may not use pathed filenames; all files\n");
				printf("must be sourced in the executable's current\n");
				printf("working directory or server's cwd\n");
				return -1;
			}
		}
	}
	
	return 0;
}
