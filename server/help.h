#ifndef HELP_H
#define HELP_H

#include <argp.h>
const char *argp_program_bug_address = "Not a bug but a feature :-)";
enum
{
    OPT_WS_IP = 1000,
    OPT_WS_PORT
};

static struct argp_option options[] =
    {
        {0, 0, 0, 0, "Options:", 1},
        {"ws-ip", OPT_WS_IP, "IP", 0, "Websocket server IP [default: 127.0.0.1]", 1},
        {"ws-port", OPT_WS_PORT, "port", 0, "Websocket server port [default: 10024]", 1},
        {0}};

#endif /* HELP_H */