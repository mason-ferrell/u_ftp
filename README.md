Basic implementation of FTP written over UDP in C

Because we are using UDP, this implementation only accepts files of size smaller than 65kb. Attempts to transmit any larger files will result in an error message and no file transmission.

Further, for security purposes, files cannot be transmitted if the handle begins with a '.' or contains a '/'. When running the client, we can only transmit files from the CWD.

To run this demo, clone this repo, and compile 'client_files/uftp_client.c' and 'server_files/uftp_server.c'. Compile uftp_server.c using '-o server', as this will keep the server binary from being viewed or tampered with. First run the server using the command '/path/to/server/exe \<port number\>', then connect to the server by using the command '/path/to/client/exe \<server IP\> \<port number\>'
