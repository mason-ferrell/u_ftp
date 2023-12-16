#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <dirent.h>
#include <sys/time.h>

#define BUFSIZE 65000

//error wrapper for syscalls
void error(char *msg) {
	fprintf(stderr, "Fatal error %s\n", msg);
	exit(-1);
}

//struct to bundle socket info together, make function calls less tedious
typedef struct {
	struct sockaddr_in clientaddr;
	struct hostent *hostp;
	char *hostaddrp;
	int clientlen;
} clientinfo;

//make life a little bit easier
void uftp_sendto(int sockfd, char *buf, int bufsize, clientinfo *c) {
	if(sendto(sockfd, buf, bufsize, 0, (struct sockaddr*)&c->clientaddr, c->clientlen) < 0)
		error("sending data to server");
}

void uftp_recvfrom(int sockfd, char *buf, clientinfo *c) {
	if(recvfrom(sockfd, buf, BUFSIZE, 0, (struct sockaddr*)&c->clientaddr, &c->clientlen) < 0)
		error("receiving data from server");
}

//prototypes for put, get, and ls functionality
void put(char*, char*, int, clientinfo*);
void get(char*, char*, int, clientinfo*);
void ls(char*, int, clientinfo*);

//this function is used in put and get
int get_file_size(FILE*);

int main(int argc, char **argv) {
	int sockfd;
	int optval;
	struct sockaddr_in serveraddr;
	clientinfo c;
	char buf[BUFSIZE];
	char *command, *filename, parsetmp[BUFSIZE];
	struct timeval timeout;
	
	if(argc != 2) {
		printf("Usage: %s <port number>\n", argv[0]);
		exit(-1);
	}
	
	//open socket
	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if(sockfd < 0) error("opening socket");
	
	optval = 1;
	if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const void *) &optval, sizeof(int)) < 0)
		error("setting reuseaddr");
	
	//setting up server address info
	bzero((char *) &serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serveraddr.sin_port = htons(atoi(argv[1]));
	
	if(bind(sockfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0) error("binding socket");
	
	c.clientlen = sizeof(c.clientaddr);
	while(1) {
		//receive command from client, populate client information
		bzero(buf, BUFSIZE);
		uftp_recvfrom(sockfd, buf, &c);
		
		c.hostp = gethostbyaddr((const char *)&c.clientaddr.sin_addr.s_addr,
					sizeof(c.clientaddr.sin_addr.s_addr), AF_INET);
		
		if(c.hostp == NULL) error("in gethostbyaddr");
		c.hostaddrp = inet_ntoa(c.clientaddr.sin_addr);
		if(c.hostaddrp == NULL) error("in inet_ntoa");
		
		//parse command and filename
		strcpy(parsetmp, buf);
		
		command = strtok(parsetmp, " \n\t");
		filename = strtok(NULL, " \n\t");
		
		bzero(buf, BUFSIZE);
		
		if(strcmp(command, "put")==0) {
			if(filename==NULL) continue;
			put(filename, buf, sockfd, &c);
		}
		
		else if(strcmp(command, "get")==0) {
			if(filename==NULL) continue;
			get(filename, buf, sockfd, &c);
		}
		
		else if(strcmp(command, "ls")==0) {
			if(filename!=NULL) continue;
			ls(buf, sockfd, &c);
		}
		
		else if(strcmp(command, "delete")==0) {
			if(filename==NULL || strcmp(filename, "server")==0) continue;

			if(remove(filename)!=0) {
				uftp_sendto(sockfd, "no file\00", 8, &c);
				continue;
			}
			uftp_sendto(sockfd, "success\00", 8, &c);
		}
		
		else if(strcmp(command, "exit")==0) {
			if(filename!=NULL) continue;
			
			strcpy(buf, "Goodbye!");
			uftp_sendto(sockfd, buf, strlen(buf), &c);
		}
	}
}

void put(char *filename, char *buf, int sockfd, clientinfo *c) {
	FILE *fp;
	int filesize;
	
	//if client tries to overwrite server, return back to main loop
	if(strcmp(filename, "server")==0) {
		return;
	}
	
	//if client has an error with the file, pass filesize -1
	//and let server return to main loop
	uftp_recvfrom(sockfd, (char*)&filesize, c);
	if(filesize==-1) {
		return;
	}
	
	//acknowledge to client that server received filesize
	uftp_sendto(sockfd, "ACK", strlen("ACK"), c);
	
	//if client can't read entire file, let server know
	//otherwise, write file to buffer
	uftp_recvfrom(sockfd, buf, c);
	if(strcmp(buf, "no cigar")==0) return;
	
	fp = fopen(filename, "wb");
	if(fp == NULL) error("opening file for put");
	
	//if server can't write entire file, let client know
	if(fwrite(buf, 1, filesize, fp) < filesize) {
		fprintf(stderr, "Couldn't write full contents of file\n");
		uftp_sendto(sockfd, "no cigar\00", 9, c);
		if(fclose(fp) != 0) error("closing file");
		return;
	}
	
	if(fclose(fp) != 0) error("closing file");
	
	uftp_sendto(sockfd, "success\00", 8, c);
}

void get(char *filename, char *buf, int sockfd, clientinfo *c) {
	FILE *fp;
	int filesize = -1;
	
	//if client tries to get server binary, return out to main loop
	if(strcmp(filename, "server")==0) return;
	
	fp = fopen(filename, "r");
	
	//if file doesn't exist, pass filesize = -1 to client
	if(fp == NULL) {
		uftp_sendto(sockfd, (char *) &filesize, sizeof(int), c);
		return;
	}
	
	filesize = get_file_size(fp);
	
	//if filesize greater than BUFSIZE, throw an error and let client know
	//with filesize = -2
	if(filesize > BUFSIZE) {
		filesize = -2;
		uftp_sendto(sockfd, (char*)&filesize, sizeof(int), c);
		if(fclose(fp) != 0) error("closing file");
		return;
	}
	
	uftp_sendto(sockfd, (char*)&filesize, sizeof(int), c);
	
	//wait for client to acknowledge it has file size
	//otherwise, could lead to race condition with file data
	uftp_recvfrom(sockfd, buf, c);
	if(strcmp(buf, "ACK")!=0) {
		fprintf(stderr, "Couldn't receive ACK\n");
		return;
	}
	
	bzero(buf, BUFSIZE);

	if(fread(buf, 1, filesize, fp) < filesize) {
		fprintf(stderr, "Error: couldn't read entire file\n");
		uftp_sendto(sockfd, "no cigar\00", 9, c);
	} else {
		uftp_sendto(sockfd, buf, filesize, c);
	}
	
	if(fclose(fp) != 0) error("closing file");
}

void ls(char* buf, int sockfd, clientinfo *c) {
	struct dirent *d;
	DIR *dh;
	char cwd[BUFSIZE];
	
	if(getcwd(cwd, sizeof(cwd)) == NULL) error("getting cwd");		
	dh = opendir(cwd);
	if(!dh) error("opening directory");
			
	strcat(buf, "\n");
	
	//for each element in current directory, add to buffer in a line
	//skip '.' and '..', as well as the server binary listing
	while((d = readdir(dh)) != NULL) {
		if(d->d_name[0] == '.') continue;
		
		if(strcmp(d->d_name, "server")==0)
			continue;
				
		strcat(buf, d->d_name);
		strcat(buf, "\n");
	}
			
	uftp_sendto(sockfd, buf, strlen(buf), c);
}

int get_file_size(FILE *fp) {
	int size;
	
	fseek(fp, 0, SEEK_END);
	size = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	
	return size;
}
