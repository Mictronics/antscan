#include "receiver.h"
#include <stdarg.h>
#include <unistd.h>

#include "real.h"

char receiver_resource[64] = "TCPIP::192.168.1.128";

#ifdef REAL
static char scpiCommand[128];
static ViSession rmHandle, scopeHandle;
static bool sessionOpen = false;
static char scpiResponse[1024];

/* Send one SCPI command, returns 0 on success */
static int scpi(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(scpiCommand, sizeof(scpiCommand), fmt, ap);
    va_end(ap);
    return viWrite(scopeHandle, (ViBuf)scpiCommand, (ViUInt32)strlen(scpiCommand), VI_NULL) < VI_SUCCESS ? -1 : 0;
}

static void close_session()
{
    if (sessionOpen)
    {
        viClose(scopeHandle);
        viClose(rmHandle);
        sessionOpen = false;
    }
}

#define CHECK(call)     \
    do                  \
    {                   \
        if ((call) != 0) \
        {               \
            return -1;  \
        }               \
    } while (0)
#endif

int init_receiver(int start_freq, int stop_freq)
{
    #ifdef REAL
    close_session();
    if (viOpenDefaultRM(&rmHandle) < VI_SUCCESS)
    {
        return -1;
    }
    if (viOpen(rmHandle, receiver_resource, VI_NULL, VI_NULL, &scopeHandle) < VI_SUCCESS)
    {
        viClose(rmHandle);
        return -1;
    }
    sessionOpen = true;
    if (viClear(scopeHandle) < VI_SUCCESS)
    {
        return -1;
    }

    //Reset
    CHECK(scpi("*RST"));

    //Spectrum Measurement
    CHECK(scpi("INST:SEL SAN"));

    //Unit: dBm
    CHECK(scpi("CALC:UNIT:POW DBM"));

    //Start frequency
    CHECK(scpi("SENS:FREQ:STAR %i MHz", start_freq));

    //Stop frequency
    CHECK(scpi("SENS:FREQ:STOP %i MHz", stop_freq));

    //Tracking generator 1 on
    CHECK(scpi("OUTP1 ON"));

    //Power mode fixed (no power sweep)
    CHECK(scpi("SOUR:POW:MODE FIX"));

    //Set source power
    CHECK(scpi("SOUR:POW 0 dBm"));

    //Calibrate transmission
    CHECK(scpi("SENS:CORR:METH TRAN"));
    sleep(1);

    //Normalization ON
    CHECK(scpi("SENS:CORR:STAT ON"));

    //Referenz level
    CHECK(scpi("DISP:TRAC:Y:RLEV 0dBm"));

    //Refernece value position
    CHECK(scpi("DISP:TRAC:Y:RPOS 50PCT"));

    sleep(1);

    //Run single
    CHECK(scpi("INIT:CONT OFF"));
    #else
    (void)start_freq;
    (void)stop_freq;
    #endif
    return 0;
}

/* Reconnect the VISA session only: no *RST, so the instrument keeps its calibration */
int reopen_receiver(void)
{
    #ifdef REAL
    close_session();
    if (viOpenDefaultRM(&rmHandle) < VI_SUCCESS)
    {
        return -1;
    }
    if (viOpen(rmHandle, receiver_resource, VI_NULL, VI_NULL, &scopeHandle) < VI_SUCCESS)
    {
        viClose(rmHandle);
        return -1;
    }
    sessionOpen = true;
    return viClear(scopeHandle) < VI_SUCCESS ? -1 : 0;
    #else
    return 0;
    #endif
}

int measure()
{
    #ifdef REAL
    //Initiate measurement
    return scpi("INIT;*WAI");
    #else
    return 0;
    #endif
}

int get_data(int measure_freq, double *value)
{
    #ifdef REAL
    ViUInt32 returnCount = 0;
    //Position marker on x-axis
    CHECK(scpi("CALC:MARK1:X %dMHz", measure_freq));

    //Read marker data
    CHECK(scpi("CALC:MARK1:Y?"));
    if (viRead(scopeHandle, (ViBuf)scpiResponse, sizeof(scpiResponse) - 1, &returnCount) < VI_SUCCESS)
    {
        return -1;
    }
    scpiResponse[returnCount] = 0;
    char *end;
    *value = strtod(scpiResponse, &end);
    return end == scpiResponse ? -1 : 0;
    #else
    (void)measure_freq;
    *value = 0;
    return 0;
    #endif
}
