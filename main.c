/**
* VDetector Handle multiple socket connections with select and fd_set on Linux
* One socket listen for command channel and another for image channel which data is 
* sent from vdetector to host.
* Everytime when host connect to VDetector, it can send "[reset]" command if there are 
* unused socket to clear all exist socket connection.
* 
* yi.zheng@outlook.com	
*/
 
#include <stdio.h>
#include <string.h>   //strlen
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>   //close
#include <arpa/inet.h>    //close
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h> //FD_SET, FD_ISSET, FD_ZERO macros
 
#define TRUE   1
#define FALSE  0
#define IMG_PORT 8888
#define CMD_PORT 9000
#define MAX_CON_NUM 10

int open_socket(port)
{
    //create a master socket
    int master_socket;
    if((master_socket = socket(AF_INET, SOCK_STREAM, 0)) == 0) 
    {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
 
    //set master socket to allow multiple connections , this is just a good habit,
    // it will work without this
    int opt = TRUE;
    if( setsockopt(master_socket, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)) < 0 )
    {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }
 
    //type of socket created
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
     
    //bind the socket to localhost port 8888
    if (bind(master_socket, (struct sockaddr *)&address, sizeof(address))<0) 
    {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
	printf("Listener on port %d \n", port);
	
    //try to specify maximum of 3 pending connections for the master socket
    if (listen(master_socket, 3) < 0)
    {
        perror("listen");
        exit(EXIT_FAILURE);
    }
     
    //accept the incoming connection
    int addrlen = sizeof(address);
    puts("Waiting for connections ...");
    return master_socket;
}


int accept_new_connection(int master_socket, int client_socket[], int max_clients)
{
    int new_socket;
    struct sockaddr_in address;
    int addrlen=0;
    if ((new_socket = accept(master_socket, (struct sockaddr *)&address, (socklen_t*)&addrlen))<0)
    {
        perror("accept");
        return -1;
    }

    //inform user of socket number - used in send and receive commands
    printf("New connection , socket fd is %d , ip is : %s , port : %d \n" , new_socket , inet_ntoa(address.sin_addr) , ntohs(address.sin_port));

    //add new socket to array of sockets
    int i=0;
    for (i = 0; i < max_clients; i++) 
    {
        //if position is empty
        if( client_socket[i] == 0 )
        {
            client_socket[i] = new_socket;
            printf("Adding to list of sockets as %d\n" , i);
            break;
        }
    }
    return 0; 
}

int process_img_data(int sd)
{
    puts("process img data");
    char* buffer="image test";
    send(sd , buffer , strlen(buffer) , 0 );
    return 0;
}

int process_cmd_data(int sd)
{
    char buffer[1025];  //data buffer of 1K
    int addrlen = 0;
    struct sockaddr_in address;
    int valread = 0;
    //Check if it was for closing , and also read the incoming message
    if ((valread = read( sd , buffer, 1024)) == 0)
    {
        //Somebody disconnected , get his details and print
        getpeername(sd , (struct sockaddr*)&address , (socklen_t*)&addrlen);
        printf("Host disconnected , ip %s , port %d \n" , inet_ntoa(address.sin_addr) , ntohs(address.sin_port));

        //Close the socket and mark as 0 in list for reuse
        close(sd);
        return -1;
    }

    //Echo back the message that came in
    else
    {
        //set the string terminating NULL byte on the end of the data read
        buffer[valread] = '\0';
        send(sd , buffer , strlen(buffer) , 0 );
    }
    return 0;
}

int main(int argc , char *argv[])
{
    int addrlen;
    int cmd_master_socket, img_master_socket, new_socket;
    int cmd_client_socket[MAX_CON_NUM] ;
    int img_client_socket[MAX_CON_NUM] ;
    int max_clients = MAX_CON_NUM;
    int activity, i , sd;
	int max_sd;
     
    //a message
    char *message = "ECHO Daemon v1.0 \r\n";
 
    //initialise all client_socket[] to 0 so not checked
    for (i = 0; i < max_clients; i++) 
    {
        cmd_client_socket[i] = 0;
        img_client_socket[i] = 0;
    }
    
    cmd_master_socket = open_socket(CMD_PORT);
    puts("open cmd port\n");
    if(cmd_master_socket < 0)
    {
        goto CLEAN;
    }

    img_master_socket = open_socket(IMG_PORT);
    puts("open image port\n");
    if(cmd_master_socket < 0)
    {
        goto CLEAN;
    }
    
    //set of socket descriptors
    fd_set read_fds;
    fd_set write_fds;
	while(TRUE) 
    {
        //clear the socket set
        FD_ZERO(&read_fds);
        FD_ZERO(&write_fds);
 
        //add master socket to set
        FD_SET(cmd_master_socket, &read_fds);
        FD_SET(img_master_socket, &read_fds);
        max_sd = cmd_master_socket > img_master_socket ? cmd_master_socket:img_master_socket;
		
        //add child sockets to set
        for ( i = 0 ; i < max_clients ; i++) 
        {
            //socket descriptor
			sd = cmd_client_socket[i];
            
			//if valid socket descriptor then add to read list
			if(sd > 0)
				FD_SET(sd , &read_fds);
            
            //highest file descriptor number, need it for the select function
            if(sd > max_sd)
				max_sd = sd;

			sd = img_client_socket[i];
            
			//if valid socket descriptor then add to read list
			if(sd > 0)
				FD_SET(sd, &write_fds);
            
            //highest file descriptor number, need it for the select function
            if(sd > max_sd)
				max_sd = sd;
        }
 
        //wait for an activity on one of the sockets 
        struct timeval timeout ;
        timeout.tv_sec = 0; 
        timeout.tv_usec =20;

        activity = select(max_sd + 1 , &read_fds , &write_fds, NULL, &timeout);
        //activity = select(max_sd + 1 , &read_fds , &write_fds, NULL, NULL);
   
        if ((activity < 0) && (errno!=EINTR)) 
        {
            printf("select error");
            goto CLEAN; 
        }
         
        //If something happened on the read master socket, then its an incoming cmd connection
        if (FD_ISSET(cmd_master_socket, &read_fds)) 
            accept_new_connection(cmd_master_socket, cmd_client_socket, MAX_CON_NUM);
        
        if (FD_ISSET(img_master_socket, &read_fds)){
            puts("img socekt get request");
            accept_new_connection(img_master_socket, img_client_socket, MAX_CON_NUM);
        }
        //else its some IO operation on some other socket :)
        for (i = 0; i < MAX_CON_NUM; i++) 
        {
            sd = cmd_client_socket[i];
            if (FD_ISSET(sd, &read_fds)) 
            {
                if (process_cmd_data(sd)<0)
                    cmd_client_socket[i] = 0;
            }

            sd = img_client_socket[i];
            if(FD_ISSET(sd, &write_fds))
            {
                if (process_img_data(sd)<0)
                    img_client_socket[i] = 0;
            }
        }
    }
    return 0;

CLEAN:
    for (i = 0; i < max_clients; i++) 
    {
        if(cmd_client_socket[i])
           close(cmd_client_socket[i]);
        if(img_client_socket[i])
           close(img_client_socket[i]);
    }
    close(cmd_master_socket);
    close(img_master_socket);
} 
