#include "head.h"

#include "real.h"

#define SOCKET_TIMEOUT_S 3
#define QUERY_RETRIES 5
#define POSITION_TOLERANCE 1 /* degree */
#define POSITION_TIMEOUT_S 60
#define ANY_POSITION INT_MIN

static uint8_t messages[6][7] = {
    {0xFF, 0x01, 0x00, 0x51, 0x00, 0x00, 0x52},
    {0xFF, 0x01, 0x00, 0x53, 0x00, 0x00, 0x54},
    {0xFF, 0x01, 0x00, 0x4B, 0x00, 0x00, 0x00},
    {0xFF, 0x01, 0x00, 0x4D, 0x00, 0x00, 0x00},
    {0xFF, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00}};

static uint8_t client_message[7], server_message[100];
static struct sockaddr_in server_addr;
static int socket_desc;

/* The head wakes up on the first command; its reply is not needed */
static void dummy_cmd()
{
    memcpy(client_message, messages[DUMMY], MSG_LENGHT);
    send_message();
    receive_response();
}

uint8_t checksum(int msg)
{
    uint8_t checksum = messages[msg][1] + messages[msg][2] + messages[msg][3] + messages[msg][4] + messages[msg][5];
    return checksum;
}

void clean()
{
    memset(server_message, '0', sizeof(server_message));
    memset(client_message, '0', sizeof(client_message));
}

int create_socket()
{
    socket_desc = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_desc < 0)
    {
        printf("Unable to create socket\n");
        return -1;
    }

    int state = 1;
    struct timeval timeout = {.tv_sec = SOCKET_TIMEOUT_S, .tv_usec = 0};
    setsockopt(socket_desc, IPPROTO_TCP, TCP_CORK, &state, sizeof(state));
    /* Timeouts also bound connect() and keep a silent head from blocking the scan forever */
    setsockopt(socket_desc, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(socket_desc, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    printf("Socket created successfully\n");

    return socket_desc;
}

int connect_server()
{
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8899);
    server_addr.sin_addr.s_addr = inet_addr("192.168.1.7");

    if (connect(socket_desc, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        printf("Unable to connect\n");
        return -1;
    }
    printf("Connected with server successfully\n");

    return 0;
}

/* Query position in degrees, retry on invalid or short response. Returns 0 on success. */
static int query_position(int msg, uint8_t reply_cmd, uint16_t *degrees)
{
    memcpy(client_message, messages[msg], MSG_LENGHT);
    for (int n = 0; n < QUERY_RETRIES; n++)
    {
        if (send_message() < 0)
        {
            return -1;
        }
        int ret = receive_response();
        if (ret >= 14 && server_message[7] == 0xff && server_message[8] == 0x01 && server_message[9] == 0x00 && server_message[10] == reply_cmd)
        {
            *degrees = ((server_message[11] << 8) + server_message[12]) / 100;
            return 0;
        }
    }
    return -1;
}

int query_pan_position(uint16_t *degrees)
{
    if (query_position(QUERY_PAN_POSITION, 0x59, degrees) != 0)
    {
        return -1;
    }
    *degrees %= 360;
    return 0;
}

int query_tilt_position(int16_t *degrees)
{
    uint16_t pos;
    if (query_position(QUERY_TILT_POSITION, 0x5b, &pos) != 0)
    {
        return -1;
    }
    *degrees = pos > 180 ? pos - 360 : pos;
    return 0;
}

#ifdef REAL
/* Shortest distance between two pan angles, handles wrap at 360 */
static int angle_distance(int a, int b)
{
    int d = (((a - b) % 360) + 360) % 360;
    return d > 180 ? 360 - d : d;
}

/* Poll until the head is within tolerance of the targets (ANY_POSITION: not checked) */
static int wait_for_position(int pan, int tilt, unsigned int poll_delay_us)
{
    time_t deadline = time(NULL) + POSITION_TIMEOUT_S;
    while (time(NULL) < deadline)
    {
        uint16_t pan_now = 0;
        int16_t tilt_now = 0;
        if (pan != ANY_POSITION && query_pan_position(&pan_now) != 0)
        {
            return -1;
        }
        if (tilt != ANY_POSITION && query_tilt_position(&tilt_now) != 0)
        {
            return -1;
        }
        if ((pan == ANY_POSITION || angle_distance(pan_now, pan) <= POSITION_TOLERANCE) &&
            (tilt == ANY_POSITION || abs(tilt_now - tilt) <= POSITION_TOLERANCE))
        {
            return 0;
        }
        usleep(poll_delay_us);
    }
    return -1;
}
#endif

int set_pan_position(int degrees)
{
    dummy_cmd();
    uint16_t buffer = (degrees % 360) * 100;
    messages[SET_PAN_POSITION][4] = (buffer >> 8) & 0xFF;
    messages[SET_PAN_POSITION][5] = buffer & 0xFF;
    messages[SET_PAN_POSITION][6] = checksum(SET_PAN_POSITION);
    memcpy(client_message, messages[SET_PAN_POSITION], MSG_LENGHT);
    if (send_message() < 0 || receive_response() < 0)
    {
        return -1;
    }
    #ifdef REAL
    return wait_for_position(degrees % 360, ANY_POSITION, 0);
    #else
    return 0;
    #endif
}

int set_tilt_position(int degrees)
{
    dummy_cmd();
    int tilt = degrees > 90 ? 90 : (degrees < -90 ? -90 : degrees);
    /* Head takes negative angles as 360 + angle, in 1/100 degree */
    uint16_t buffer = (tilt < 0 ? tilt + 360 : tilt) * 100;
    messages[SET_TILT_POSITION][4] = (buffer >> 8) & 0xFF;
    messages[SET_TILT_POSITION][5] = buffer & 0xFF;
    messages[SET_TILT_POSITION][6] = checksum(SET_TILT_POSITION);
    memcpy(client_message, messages[SET_TILT_POSITION], MSG_LENGHT);
    if (send_message() < 0 || receive_response() < 0)
    {
        return -1;
    }
    #ifdef REAL
    return wait_for_position(ANY_POSITION, tilt, 0);
    #else
    return 0;
    #endif
}

int self_check()
{
    client_message[0] = 0xFF;
    client_message[1] = 0x01;
    client_message[2] = 0x00;
    client_message[3] = 0x07;
    client_message[4] = 0x00;
    client_message[5] = 0x77;
    client_message[6] = 0x7f;
    if (send_message() < 0 || receive_response() < 0)
    {
        return -1;
    }
    #ifdef REAL
    sleep(1);
    return wait_for_position(0, 0, 1000000);
    #else
    return 0;
    #endif
}

int home()
{
    return set_pan_position(0) | set_tilt_position(0);
}

int send_message()
{
    #ifdef REAL
    ssize_t len = send(socket_desc, client_message, MSG_LENGHT, MSG_NOSIGNAL);
    if (len != MSG_LENGHT)
    {
        printf("Unable to send message\n");
        return -1;
    }
    #endif

    return 0;
}

int receive_response()
{
    #ifdef REAL
    int response_length = recv(socket_desc, server_message, sizeof(server_message), 0);
    if (response_length <= 0)
    {
        printf("Error while receiving server's msg\n");
        return -1;
    }
    return response_length;
    #else
    return 0;
    #endif
}

void close_socket()
{
    close(socket_desc);
}
