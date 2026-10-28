#ifndef HELP_H
#define HELP_H

#include <argp.h>
const char *argp_program_bug_address = "Not a bug but a feature :-)";
enum
{
    OPT_WS_IP = 1000,
    OPT_WS_PORT,
    OPT_HEAD_IP,
    OPT_HEAD_PORT,
    OPT_RECEIVER
};

static struct argp_option options[] =
    {
        {0, 0, 0, 0, "Options:", 1},
        {"ws-ip", OPT_WS_IP, "IP", 0, "Websocket server IP [default: 127.0.0.1]", 1},
        {"ws-port", OPT_WS_PORT, "port", 0, "Websocket server port [default: 10024]", 1},
        {"head-ip", OPT_HEAD_IP, "IP", 0, "IP of pan-tilt head [default: 192.168.1.7]", 1},
        {"head-port", OPT_HEAD_PORT, "port", 0, "Port of pan-tilt head [default: 8899]", 1},
        {"receiver", OPT_RECEIVER, "resource", 0, "VISA resource of receiver [default: TCPIP::192.168.1.128]", 1},
        {0}};

#endif /* HELP_H */