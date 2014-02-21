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
#include <signal.h> 
#define TRUE   1
#define FALSE  0
#define IMG_PORT 4001 
#define CMD_PORT 3000
#define MAX_CON_NUM 10
#define CMD_BUF_SIZE 128
#define PARA_BUF_SIZE 32

#define STX '['
#define ETX ']'

#define ERR_FORMAT -1
#define PIXEL_NUM 1536
#define PATTERN_BUF_SIZE  (2*PIXEL_NUM)
#define PATTERN_BASE  1500
#define PATTERN_STEP  10

int g_integration_time = 5000;
int g_data_pattern_enabled = FALSE;
int g_sensitivity_level = 6;
int g_frame_enabled = FALSE;

unsigned char g_cmd_buf[CMD_BUF_SIZE-1];  //data buffer of 1K
unsigned char g_cmd_response[CMD_BUF_SIZE-1];
unsigned char g_param_1 [PARA_BUF_SIZE-1];
unsigned char g_data_pattern_buf [PATTERN_BUF_SIZE-1];
unsigned char g_data_header[6];

unsigned char packet_count = 0;

void cmd_handler(char* cmd, char* cmd_response);

void set_packet_count()
{
    packet_count++;
    g_data_header[3] = packet_count;
}

void init_data_header()
{
    g_data_header[0] = 0;
    unsigned int len = PIXEL_NUM*2+6;
    g_data_header[1] = (len & 0xFF00)>>8;
    g_data_header[2] = (len & 0xFF);
    g_data_header[3] = packet_count;
    g_data_header[4] = 0;
    g_data_header[5] = 0;
}

void init_pattern_buf (void)
{
    int i;
    for (i=0; i < PIXEL_NUM; i++)
    {
        unsigned int data = PATTERN_BASE + PATTERN_STEP*i;
        g_data_pattern_buf[i*2] = (data & 0xFF);
        g_data_pattern_buf[i*2+1] = (data & 0xFF00) >> 8;

    }
}

struct timeval t_last_time;

double time_diff(struct timeval x , struct timeval y)
{
    double x_ms , y_ms , diff;
          
    x_ms = (double)x.tv_sec*1000000 + (double)x.tv_usec;
    y_ms = (double)y.tv_sec*1000000 + (double)y.tv_usec;
                    
    diff = (double)y_ms - (double)x_ms;
                        
    return diff;
}

int process_img_data(int sd)
{
    //puts("process img data");
    unsigned char* buf= NULL;
    struct timeval t_current;
    gettimeofday (&t_current, NULL);
    double diff = time_diff(t_last_time, t_current);

    if (diff  < g_integration_time)
        return 0;

    if (g_frame_enabled)
    {
        if (g_data_pattern_enabled)
        {
            buf = g_data_pattern_buf;
        }
        else {
            buf = g_data_pattern_buf;
        }
        //char* address;
        //int len = get_data_buf(&address);
        //if (len == 0)
        //    return 0;
        if (send(sd , g_data_header, 6, 0) < 0) 
        {
            printf("Send header error\n");
            close(sd);
            return -1;
        };
        if (send(sd , buf, PIXEL_NUM*2, 0) < 0) 
        {
            printf("Send data error\n");
            close(sd);
            return -1;
        };
        set_packet_count();
    }
    t_last_time = t_current;
    return 0;
}

int process_cmd_data(int sd)
{
    int addrlen = 0;
    struct sockaddr_in address;
    int valread = 0;
    //Check if it was for closing , and also read the incoming message
    if ((valread = read( sd , g_cmd_buf, CMD_BUF_SIZE-1)) == 0)
    {
        //Somebody disconnected , get his details and print
        getpeername(sd , (struct sockaddr*)&address , (socklen_t*)&addrlen);
        printf("Host disconnected , ip %s , port %d \n" , inet_ntoa(address.sin_addr) , ntohs(address.sin_port));
        //Restart tcp server
        close(sd);
        return -1;
    }
    //Process command
    else
    {
        //set the string terminating NULL byte on the end of the data read
        g_cmd_buf[valread] = '\0';
        printf("get cmd %s\n", g_cmd_buf);
        cmd_handler(g_cmd_buf,g_cmd_response);
        if(send(sd , g_cmd_response , strlen(g_cmd_response) , 0 ) < 0){
            close(sd);
            return -1;
        }
    }
    return 0;
}


    /* SF, 1/0  StartFrame StopFrame
       IN, 1/0  Initialize
       RI       Retrive Info
       ST, W/R, time in us  
       ED, W/R, 0/1
       SS, W/R, 1-7
       TP, W/R, time ms

       */
    /*
       For testing purpose
       XO, image_id  x-ray on image_id
       XF, x-ray off load offset image
       OS, image_id  Object Scan with x-ray

       */
enum Command {
    cmd_UNKNOWN,
    cmd_ED,
    cmd_IN,
    cmd_RI,
    cmd_SF,
    cmd_SS,
    cmd_ST,
    cmd_TP
#ifndef NIOS
    , 
    cmd_XO,
    cmd_XF,
    cmd_OS
#endif
};

enum Operation {
    op_UNKNOWN,
    op_R,
    op_W,
    op_S,
    op_L,
    op_Enable,
    op_Disable
};

int check_start_end_flag (char* cmd)
{
    int len = strlen(cmd);
    if (cmd[0] == STX)
    {
        int i =len;
        while (i)
        {
            if (cmd[i] == ETX)
            {
                cmd[i+1] = 0;
                break;
            }
            i--;
        }
        if (i > 0)
            return TRUE;
    }
    return FALSE;
}

void response_unknown (char* buf)
{
    sprintf (buf, "[4]");
}

void response_ok (char* buf)
{
    sprintf (buf, "[0]");
}

void response_ok_param1 (char* buf, int param1)
{
    sprintf (buf, "[0, %d]", param1);
}

void response_detector_info (char* buf)
{
    sprintf (buf, "[0]");
}

enum Command get_cmd_id (char* cmd)
{
    /*
    cmd_ED,
    cmd_IN,
    cmd_RI,
    cmd_SF,
    cmd_SS,
    cmd_ST*/

    if ((cmd[1] == 'E')&&(cmd[2] == 'D'))
        return cmd_ED;

    if ((cmd[1] == 'I')&&(cmd[2] == 'N'))
        return cmd_IN;

    if ((cmd[1] == 'R')&&(cmd[2] == 'I'))
        return cmd_RI;

    if ((cmd[1] == 'S')&&(cmd[2] == 'F'))
        return cmd_SF;

    if ((cmd[1] == 'S')&&(cmd[2] == 'S'))
        return cmd_SS;

    if ((cmd[1] == 'S')&&(cmd[2] == 'T'))
        return cmd_ST;

    if ((cmd[1] == 'T')&&(cmd[2] == 'P'))
        return cmd_ST;
#ifndef NIOS
    if ((cmd[1] == 'X')&&(cmd[2] == 'O'))
        return cmd_XO;
    if ((cmd[1] == 'X')&&(cmd[2] == 'F'))
        return cmd_XF;
    if ((cmd[1] == 'O')&&(cmd[2] == 'S'))
        return cmd_OS;
#endif
    return cmd_UNKNOWN;
}

enum Operation get_op_id (char* cmd)
{
    if (cmd[4] == 'W')
        return op_W;
    if (cmd[4] == 'R')
        return op_R;
    if (cmd[4] == '1')
        return op_Enable;
    if (cmd[4] == '0')
        return op_Disable;
    return op_UNKNOWN;
}

int digital (char c)
{
    if ((c >= '0') && (c <= '9')){
        return (c-'0');
    }
    else if((c >= 'A')&&(c <= 'F')) {
        return (10 + c - 'A');
    }
    else if((c >= 'a') && (c <= 'f')) {
        return (10 + c - 'a');
    }

    return -1;
}

int get_param_1 (char* cmd)
{
    //Find the second ','
    if (cmd[5] != ',')
        return 0;
    int start, end, n, result, max; 
    start = 6;
    end = start;
    max = start + 6;
    n = 0;
    result = 0;
    while (1)
    {
        n = digital(cmd[end]);
        if (n < 0)
            break;
        result = result*16 + n;
        end++;
        if (end > max)
            break;
    }
    return result;
}

void start_frame ()
{
    packet_count = 0;
    g_frame_enabled = TRUE;
    gettimeofday (&t_last_time, NULL);
    printf("last time is %f s    %f us", (double)t_last_time.tv_sec, (double)t_last_time.tv_usec);
}

void stop_frame ()
{
    g_frame_enabled = FALSE;
}

void enable_data_pattern ()
{
    g_data_pattern_enabled = TRUE;
}

void disable_data_pattern ()
{
    g_data_pattern_enabled = FALSE;
}

void set_sensitivity_level (int level)
{
    if (level < 0)
        level = 0;
    if (level > 6)
        level = 6;
    g_sensitivity_level = level;
}

void set_integration_time (int time_in_us)
{
    if(time_in_us < 2000){
        printf("integration time is %d us, too short", time_in_us);
        return;
    }
    g_integration_time = time_in_us;

}
void cmd_handler(char* cmd, char* cmd_response)
{
#ifndef NIOS
   int image_id = 0;
#endif
    int len = strlen(cmd);
    
    if (!check_start_end_flag(cmd))
    {
        response_unknown(cmd_response);
    }

    enum Command cmd_id;
    enum Operation op_id;

    cmd_id = get_cmd_id (cmd);
    op_id = get_op_id (cmd);
    switch (cmd_id) 
    {
        case cmd_UNKNOWN:
            response_unknown(cmd_response);
            break;

        case cmd_IN:
            response_ok(cmd_response);
            break;

        case cmd_ED:
            if (op_id == op_W)
            {
                int param1 = get_param_1 (cmd);
                if (param1 > 0)
                    enable_data_pattern ();
                else
                    disable_data_pattern ();
                response_ok(cmd_response);
            } 
            else if (op_id == op_R)
            {
                response_ok_param1(cmd_response, g_data_pattern_enabled);
            }
            else if (op_id == op_UNKNOWN){
                response_unknown(cmd_response);
            }
            break;

        case cmd_RI:
            response_detector_info (cmd_response);
            break;

        case cmd_SF:
            if (op_id == op_Enable) {
                printf("Start Frame\n");
                start_frame ();
            }
            else if (op_id == op_Disable){
                printf("Stop Frame\n");
                stop_frame ();
            }

            response_ok(cmd_response);
            break;

        case cmd_ST:
            if (op_id == op_W)
            {
                int time_in_us = get_param_1 (cmd);
                printf("integtaion time is %d us\n", time_in_us);
                set_integration_time (time_in_us);
                response_ok(cmd_response);
            } 
            else if (op_id == op_R)
            {
                response_ok_param1(cmd_response, g_integration_time);
            }
            else if (op_id == op_UNKNOWN){
                response_unknown(cmd_response);
            }
            break;

        case cmd_SS:
            if (op_id == op_W){
                int sensitivity = get_param_1 (cmd);
                set_sensitivity_level (sensitivity);
                response_ok(cmd_response);
            } 
            else if (op_id == op_R){
                response_ok_param1(cmd_response, g_sensitivity_level);
            }
            else if (op_id == op_UNKNOWN){
                response_unknown(cmd_response);
            }
            break;
        case cmd_TP:
            break;
#ifndef NIOS
        case cmd_XO:
            image_id = get_param_1 (cmd);
            set_image_source(AirScan, image_id);
            break;
        case cmd_XO:
            set_image_source(Offset, 0);
            break;
        case cmd_OS:
            image_id = get_param_1 (cmd);
            set_image_source(ObjectScan, image_id);
            break;
#endif
    }
}

typedef enum { Real, Pattern, AirScan, Offset, ObjectScan} ImageDataType  ;

void set_image_source(ImageDataType data_type, int image_id)
{
    switch (data_type):
    {
        case Real:
            break;
        case Pattern:
            break;
        case Offset:
            break;
        case AirScan:
            break;
        case ObjectScan:
            break;
    }

}

void sig_pipe(int signo)
{
    printf("Get SIGPIPE signal\n");
}

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
        return -1;
    }
	printf("Listener on port %d \n", port);
	
    //try to specify maximum of 3 pending connections for the master socket
    if (listen(master_socket, 3) < 0)
    {
        perror("listen");
        return -1;
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


int main(int argc , char *argv[])
{
    int addrlen;
    int cmd_master_socket, img_master_socket, new_socket;
    int cmd_client_socket[MAX_CON_NUM] ;
    int img_client_socket[MAX_CON_NUM] ;
    int max_clients = MAX_CON_NUM;
    int activity, i , sd;
	int max_sd;

    char *message = "ECHO Daemon v1.0 \r\n";
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    if (signal(SIGPIPE,sig_pipe) == SIG_ERR) {
        printf("register error handler error,exit");
        return -1;
    }
INIT:    
    packet_count = 0;
    init_data_header();
    init_pattern_buf(); 
//init_sys();     
    //a message
 
    //initialise all client_socket[] to 0 so not checked
    for (i = 0; i < max_clients; i++) {
        cmd_client_socket[i] = 0;
        img_client_socket[i] = 0;
    }
    
    cmd_master_socket = open_socket(CMD_PORT);
    puts("open cmd port\n");
    if(cmd_master_socket < 0){
        goto CLEAN;
    }

    img_master_socket = open_socket(IMG_PORT);
    puts("open image port\n");
    if(cmd_master_socket < 0){
        goto CLEAN;
    }
    
    //set of socket descriptors
    fd_set read_fds;
    fd_set write_fds;
	while(TRUE){
        //clear the socket set
        FD_ZERO(&read_fds);
        FD_ZERO(&write_fds);
 
        //add master socket to set
        FD_SET(cmd_master_socket, &read_fds);
        FD_SET(img_master_socket, &read_fds);
        max_sd = cmd_master_socket > img_master_socket ? cmd_master_socket:img_master_socket;
		
        //add child sockets to set
        for ( i = 0 ; i < max_clients ; i++) {
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
   
        if ((activity < 0) && (errno!=EINTR)){
            printf("select error");
            goto CLEAN; 
        }
         
        //If something happened on the read master socket, then its an incoming cmd connection
        if (FD_ISSET(cmd_master_socket, &read_fds)){
            puts("img socekt get request");
            accept_new_connection(cmd_master_socket, cmd_client_socket, MAX_CON_NUM);
        }
        
        if (FD_ISSET(img_master_socket, &read_fds)){
            puts("img socekt get request");
            accept_new_connection(img_master_socket, img_client_socket, MAX_CON_NUM);
        }
        //else its some IO operation on some other socket :)
        for (i = 0; i < MAX_CON_NUM; i++){
            sd = cmd_client_socket[i];
            if (FD_ISSET(sd, &read_fds)){
                if (process_cmd_data(sd)<0){
                //   cmd_client_socket[i] = 0;
                    goto CLEAN;
                }
            }

            sd = img_client_socket[i];
            if(FD_ISSET(sd, &write_fds)){
                if (process_img_data(sd)<0){
                 //   img_client_socket[i] = 0;
                    goto CLEAN;
                }
            }
        }
    }
    printf("exit detector\n");
    return 0;

CLEAN:
    for (i = 0; i < max_clients; i++){
        if(cmd_client_socket[i])
           close(cmd_client_socket[i]);
        if(img_client_socket[i])
           close(img_client_socket[i]);
    }
    close(cmd_master_socket);
    close(img_master_socket);
    goto INIT;
} 
