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
        {"ws-ip", OPT_WS_IP, "IP", OPTION_ARG_OPTIONAL, "IP of pan-tilt head [default: 192.168.07]", 1},
        {"ws-port", OPT_WS_PORT, "port", OPTION_ARG_OPTIONAL, "Port of pan-tilt head [default: 8126]", 1},
        {0}};

#endif /* HELP_H */