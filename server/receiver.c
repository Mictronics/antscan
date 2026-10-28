#include "receiver.h"
#include <stdarg.h>
#include <unistd.h>

#include "real.h"

#ifdef REAL
static char idnQuery[128];
static ViStatus status = VI_SUCCESS;
static ViUInt32 returnCount = 0;
static ViSession rmHandle, scopeHandle;
static char idnResponse[1024];
static char ressourceString1[] = "TCPIP::192.168.1.128";

static void scpi(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(idnQuery, sizeof(idnQuery), fmt, ap);
    va_end(ap);
    status = viWrite(scopeHandle, (ViBuf)idnQuery, (ViUInt32)strlen(idnQuery), VI_NULL);
}
#endif

void init_receiver(int start_freq, int stop_freq)
{
    #ifdef REAL
    status = viOpenDefaultRM (&rmHandle);

    status = viOpen(rmHandle, ressourceString1, VI_NULL, VI_NULL, &scopeHandle);
    status = viClear(scopeHandle);

    //Reset
    scpi("*RST");

    //Spectrum Measurement
    scpi("INST:SEL SAN");

    //Unit: dBm
    scpi("CALC:UNIT:POW DBM");

    //Start frequency
    scpi("SENS:FREQ:STAR %i MHz", start_freq);

    //Stop frequency
    scpi("SENS:FREQ:STOP %i MHz", stop_freq);

    //Tracking generator 1 on
    scpi("OUTP1 ON");

    //Power mode fixed (no power sweep)
    scpi("SOUR:POW:MODE FIX");

    //Set source power
    scpi("SOUR:POW 0 dBm");

    //Calibrate transmission
    scpi("SENS:CORR:METH TRAN");
    sleep(1);

    //Normalization ON
    scpi("SENS:CORR:STAT ON");

    //Referenz level
    scpi("DISP:TRAC:Y:RLEV 0dBm");

    //Refernece value position
    scpi("DISP:TRAC:Y:RPOS 50PCT");

    sleep(1);

    //Run single
    scpi("INIT:CONT OFF");
    #else
    (void)start_freq;
    (void)stop_freq;
    #endif
}

void measure()
{
    #ifdef REAL
    //Initiate measurement
    scpi("INIT;*WAI");
    #endif
}

double get_data(int measure_freq)
{
    #ifdef REAL
    //Position marker on x-axis
    scpi("CALC:MARK1:X %dMHz", measure_freq);

    //Read marker data
    scpi("CALC:MARK1:Y?");
    status = viRead(scopeHandle, (ViBuf)idnResponse, 1024, &returnCount);
    idnResponse[returnCount] = 0;
    return strtod(idnResponse, NULL);
    #else
    (void)measure_freq;
    return 0;
    #endif
}