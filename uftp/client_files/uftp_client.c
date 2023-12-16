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

#define BUFSIZE 65000

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

//checkCommand handles errors for command input before sending to server
int checkCommand(char*, char*, char**, char**);

int main(int argc, char **argv) {
	int sockfd;
	serverinfo s;
	char buf[BUFSIZE];
	char *command, *filename;
	char parsetmp[BUFSIZE];
	
	//make sure correct number of arguments are passed
	if(argc != 3) {
		printf("Usage: %s <server IP> <server port>\n", argv[0]);
		exit(-1);
	}
	
	struct timeval timeout;
	timeout.tv_sec = 1;
	timeout.tv_usec = 0;
	
	//create socket, set timeout option should server fail to respond
	//or should client be connected to wrong IP address or port
	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if(sockfd < 0) error("opening socket");
	if(setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO,&timeout,sizeof(timeout)) < 0)
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
	
	while(1) {
		printf(">");
	
		bzero(buf, BUFSIZE);
		fgets(buf, BUFSIZE, stdin);
		
		//if there is an error, return to start of loop
		if(checkCommand(buf, parsetmp, &command, &filename) < 0)
			continue;
		
		//if filenames are safe and input meets basic requirements,
		//go ahead and send command and relevant filenames to server
		uftp_sendto(sockfd, buf, strlen(buf), &s);
			
		//zero out the buffer
		bzero(buf, BUFSIZE);

		if(strcmp(command, "put")==0) {
			//if no filename is with put, give an error
			if(filename==NULL) {
				fprintf(stderr, "Error: you must enter a filename with put\n");
				continue;
			}
			put(filename, buf, sockfd, &s);
		}
		
		else if(strcmp(command, "get")==0) {
			//if no filename is with get, give an error
			if(filename==NULL) {
				fprintf(stderr, "Error: you must enter a filename with get\n");
				continue;
			}
			get(filename, buf, sockfd, &s);
		}
		
		else if(strcmp(command, "ls")==0) {
			//if any parameters are passed with ls, throw an error
			if(filename!=NULL) {
				fprintf(stderr,"Usage: ls\n");
				continue;
			}
			uftp_recvfrom(sockfd, buf, &s);
			printf("%s\n", buf);
		}
		
		else if(strcmp(command, "delete")==0) {
			//if no file is with delete, give an error
			if(filename==NULL) {
				fprintf(stderr, "Error: you must enter a filename with delete\n");
				continue;
			}
			
			//if client tries to delete server binary, throw an error
			//note: binary must be named "server"
			if(strcmp(filename, "server")==0) {
				fprintf(stderr,"Error: invalid command\n");
				continue;
			}
			
			//if server can't find file, let client know
			//also let client know if successful
			uftp_recvfrom(sockfd, buf, &s);
			if(strcmp(buf, "success")==0) {
				printf("delete successful\n");
				continue;
			}
			if(strcmp(buf, "no file")==0)
				fprintf(stderr,"file does not exist\n");
		}
		
		else if(strcmp(command, "exit")==0) {
			//if any parameters are passed with exit, throw an error
			if(filename!=NULL) {
				fprintf(stderr, "Usage: exit\n");
				continue;
			}
			uftp_recvfrom(sockfd, buf, &s);
			printf("%s\n", buf);
			exit(0);
		} else {
			fprintf(stderr,"Please enter a valid command\n");
		}
	}
}


//put - this function takes a file sourced in the client's cwd
//and writes that file to the server's cwd
//note that it will overwrite files
void put(char *filename, char *buf, int sockfd, serverinfo *s) {
	FILE *fp;
	int filesize = -1;
	
	//if client tries to overwrite server binary, throw an error
	//note the binary must be named "server"
	if(strcmp(filename, "server")==0) {
		printf("You cannot access that file\n");
		return;
	}
	
	fp = fopen(filename, "r");
	
	//if file does not exist in client's cwd, throw an error and let server know
	if(fp == NULL) {
		fprintf(stderr,"Error: please check file exists\n");
		uftp_sendto(sockfd, (char *) &filesize, sizeof(int), s);
		return;
	}
	
	filesize = get_file_size(fp);
	
	//if filesize greater than BUFSIZE, throw error and let server know
	if(filesize > BUFSIZE) {
		fprintf(stderr,"File too large to transfer\n");
		filesize = -1;
		uftp_sendto(sockfd, (char*)&filesize, sizeof(int), s);
		if(fclose(fp) != 0) error("closing file");
		return;
	}
	
	//if file exists, pass real file size to server
	uftp_sendto(sockfd, (char*)&filesize, sizeof(int), s);
	
	//wait for server to acknowledge it has file size
	//otherwise, could lead to race condition with file data
	uftp_recvfrom(sockfd, buf, s);
	if(strcmp(buf, "ACK")!=0) {
		fprintf(stderr, "Couldn't receive ACK\n");
		if(fclose(fp) != 0) error("closing file");
		return;
	}
	
	bzero(buf, BUFSIZE);

	//if we can't read the file, throw an error and let server know
	if(fread(buf, 1, filesize, fp) < filesize) {
		fprintf(stderr, "Error: couldn't read entire file\n");
		uftp_sendto(sockfd, "no cigar\00", 9, s);
		if(fclose(fp) != 0) error("closing file");
		return;
	}
	
	if(fclose(fp) != 0) error("closing file");
	
	//send contents of file
	uftp_sendto(sockfd, buf, filesize, s);
	
	//find out if server successfully wrote file to its local directory
	//and let client know
	uftp_recvfrom(sockfd, buf, s);
	if(strcmp(buf,"success")==0) {
		printf("put successful\n");
		return;
	} else {
		printf("put unsuccessful\n");
		return;
	}
}

void get(char *filename, char *buf, int sockfd, serverinfo *s) {
	FILE *fp;
	int filesize;
	
	//if client tries to obtain server binary, throw an error
	if(strcmp(filename, "server")==0) {
		fprintf(stderr, "Error: you cannot access that file\n");
		return;
	}
	
	//if server has an error with the file, pass filesize -1
	//and let client return to main loop
	uftp_recvfrom(sockfd, (char*)&filesize, s);
	if(filesize==-1) {
		fprintf(stderr,"Please enter a valid filename (check files with ls)\n");
		return;
	}
	
	//if file too large, pass filesize -2
	if(filesize == -2) {
		fprintf(stderr, "File too large to transfer\n");
		return;
	}
	
	//acknowledge client received filesize
	uftp_sendto(sockfd, "ACK", strlen("ACK"), s);
	
	//receive file data, if server couldn't read file, poison pill string
	//"no cigar" lets client know to throw an error
	uftp_recvfrom(sockfd, buf, s);
	if(strcmp(buf, "no cigar")==0) {
		fprintf(stderr, "Couldn't read full contents of file\n");
		return;
	}
	
	fp = fopen(filename, "wb");	
	if(fp == NULL) error("opening file for put");
	
	//write buffer to file
	if(fwrite(buf, 1, filesize, fp) < filesize) {
		fprintf(stderr, "Couldn't write full contents of file\n");
		if(fclose(fp) != 0) error("closing file");
		return;
	}
	
	if(fclose(fp) != 0) error("closing file");
	printf("get successful\n");
}

//gets size of a file for non-txt files
int get_file_size(FILE *fp) {
	int size;
	
	fseek(fp, 0, SEEK_END);
	size = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	
	return size;
}

//checks that input is valid
//returns -1 if input is invalid, 0 if input is valid
int checkCommand(char *buf, char *parsetmp, char **command, char **filename) {
	//check if input went past buffer, return if so
	if(strlen(buf)==BUFSIZE-1 && buf[BUFSIZE - 2]!='\n') {
		fprintf(stderr, "Command input too long!\n");
		return -1;
	}
	
	//if no commands are given, return to main loop
	if(strcmp(buf, "\n")==0) {
		printf("You must enter a command\n");
		return -1;
	}
	
	char *error;
	strcpy(parsetmp, buf);
	
	//we pass the function for the client to execute in command
	//and any relevant files in filename, both pointers to strings
	//used in main
	*command = strtok(parsetmp, " \n\t");
	*filename = strtok(NULL, " \n\t");
	
	//if error is not null, there's three separate strings with whitespace
	//as a delimiter; give an error
	error = strtok(NULL, " \n\t");
	if(error!=NULL) {
		fprintf(stderr, "Error: too many commands given\n");
		return -1;
	}
	
	//if filename does exist, make sure no directories outside cwd are accessible
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
