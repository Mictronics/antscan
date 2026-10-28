#include "loop.h"
#include "stdint.h"
#include "uv.h"
#include <stdbool.h>
#include <stdatomic.h>
#include <libwebsockets.h>
#include "antscan.h"
#include "time.h"
#include "head.h"
#include "receiver.h"

#include "real.h"
#include <math.h>

extern int start_f, stop_f, freq_counter;
extern char file_name[25];
extern uint16_t start_azimut, stop_azimut, n;
extern int16_t start_elev, stop_elev, resolution_elev, m, azimut_sector, resolution_azimut;
extern bool serpentine;
extern Frequency* freq;
extern uv_mutex_t lock_pause_measurement;
extern uv_cond_t cond_resume_measurement;
extern atomic_bool measurement_thread_exit;
extern bool measurement_thread_pause;
extern _Atomic scan_status_e scan_status;
extern double time_paused;
double totalSteps, currentStep, azimutSteps, elevSteps, progress;
#define MAX_AZ_STEPS 360
#ifndef REAL
/* Simulated antenna: Gaussian lobe on boresight (az 0, el 0), peak in dB, beamwidths are 1-sigma in degrees */
#define SIM_PEAK_DB 10.0
#define SIM_SIGMA_AZ 25.0
#define SIM_SIGMA_EL 20.0
#endif
double startTime, currentTime, timeOneStep, totalTime, timeLeft, elapsedTime;

#define RETRIES 3
#define RETRY_DELAY_MS 2000

typedef enum
{
    OP_TILT,
    OP_PAN,
    OP_MEASURE,
    OP_DATA
} step_op_e;

static int run_op(step_op_e op, int arg, double *val)
{
    switch (op)
    {
    case OP_TILT:
        return set_tilt_position(arg);
    case OP_PAN:
        return set_pan_position(arg);
    case OP_MEASURE:
        return measure();
    default:
        return get_data(arg, val);
    }
}

/**
 * Run one head/receiver call, repeating it after a reconnect when it fails. All calls are safe to
 * repeat (absolute positions, no instrument reset). Returns -1 when the retries are used up.
 */
static int try_op(step_op_e op, int arg, double *val)
{
    const bool head = op == OP_TILT || op == OP_PAN;
    for (int attempt = 0;; attempt++)
    {
        if (run_op(op, arg, val) == 0)
        {
            return 0;
        }
        if (attempt == RETRIES || measurement_thread_exit)
        {
            return -1;
        }
        char msg[48];
        snprintf(msg, sizeof msg, "%s not responding, retry %d/%d", head ? "Pan-tilt head" : "Receiver", attempt + 1, RETRIES);
        app_message(msg, MSG_WARNING);
        uv_sleep(RETRY_DELAY_MS);
        if ((head ? head_reconnect() : reopen_receiver()) != 0)
        {
            lwsl_notice("Reconnect failed\n");
        }
    }
}

/**
 * Like try_op, but when the retries are used up the scan pauses and waits for the operator to
 * resume, then tries again. Returns -1 only when the scan was stopped.
 */
static int do_op(step_op_e op, int arg, double *val)
{
    while (try_op(op, arg, val) != 0)
    {
        if (measurement_thread_exit)
        {
            return -1;
        }
        scan_pause();
        app_message("Hardware not responding. Scan paused, fix it and resume to retry", MSG_DANGER);
        uv_mutex_lock(&lock_pause_measurement);
        while (measurement_thread_pause != false)
        {
            uv_cond_wait(&cond_resume_measurement, &lock_pause_measurement);
        }
        uv_mutex_unlock(&lock_pause_measurement);
        if (measurement_thread_exit)
        {
            return -1;
        }
    }
    return 0;
}

static void scan_error(const char *msg)
{
    app_message(msg, MSG_DANGER);
    measurement_thread_exit = true;
}

int file_init()
{
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    for(int b = 0; b < freq_counter; b++)
    {
        if(file_name[0] == '\0')
        {
            snprintf(freq[b].file_path, FILENAME_MAX, "./server/data/scan_%02u_%02u_%04u_%02u_%02u_%02u_%dMHz.csv",
            t->tm_mday,
            t->tm_mon + 1,
            t->tm_year + 1900,
            t->tm_hour,
            t->tm_min,
            t->tm_sec,
            freq[b].frequency);
        } else
        {
            snprintf(freq[b].file_path, FILENAME_MAX, "./server/data/%s_%dMHz.csv", file_name, freq[b].frequency);
        }

        freq[b].file_stream = fopen(freq[b].file_path, "w");
        if(freq[b].file_stream == NULL)
        {
            free_freq();
            return -1;
        }

        fprintf(freq[b].file_stream, "START_FREQ:%d;STOP_FREQ:%d;MEASURE_FREQ:%d;REF_GAIN:%lf;\n", start_f, stop_f, freq[b].frequency, freq[b].reference_gain);
        fputs("TILT/PAN;", freq[b].file_stream);
        for(int i = 0; i <= azimut_sector; i += resolution_azimut)
        {
            if(start_azimut >= 180)
            {
                fprintf(freq[b].file_stream, "%d;", (start_azimut - 360 + i) % 360);
            } else
            {
                fprintf(freq[b].file_stream, "%d;", (((start_azimut - stop_azimut) / 2) + i) % 360);
            }
        }
        fputs("\n", freq[b].file_stream);
    }
    return 0;
}

void measurement_loop()
{
    thread_to_core(2);
    currentStep = 0;
    progress = 0;
    azimutSteps = (azimut_sector / resolution_azimut) + 1;
    elevSteps = (abs(start_elev - stop_elev) / resolution_elev) + 1;
    totalSteps = azimutSteps * elevSteps;
    startTime = (double)time(NULL);
    /* One elevation row of values per frequency, written in ascending azimuth order */
    double (*row)[MAX_AZ_STEPS] = calloc(freq_counter, sizeof *row);
    if (row == NULL)
    {
        scan_error("Out of memory");
        return;
    }
    for(m = start_elev; m <= stop_elev; m += resolution_elev)
    {
            if (do_op(OP_TILT, m, NULL) != 0)
            {
                break;
            }
            bool reversed = serpentine && ((m - start_elev) / resolution_elev) % 2 == 1;
            int measured = 0;
            for(n = 0; n <= azimut_sector; n += resolution_azimut)
            {
                /* Azimuth offset in the sector; odd rows run backwards when serpentine */
                int a = reversed ? azimut_sector - azimut_sector % resolution_azimut - n : n;
                if (do_op(OP_PAN, start_azimut + a, NULL) != 0 || do_op(OP_MEASURE, 0, NULL) != 0)
                {
                    break;
                }
                currentStep += 1;
                progress = 100 * (currentStep / totalSteps);
                for(int d = 0; d < freq_counter; d++)
                {
                    double val;
                    #ifndef REAL
                    double da = (a - azimut_sector / 2.0) / SIM_SIGMA_AZ, de = m / SIM_SIGMA_EL;
                    val = SIM_PEAK_DB * exp(-0.5 * (da * da + de * de)) + freq[d].reference_gain;
                    #else
                    if (do_op(OP_DATA, freq[d].frequency, &val) != 0)
                    {
                        break;
                    }
                    val += freq[d].reference_gain;
                    #endif

                    row[d][a / resolution_azimut] = val;

                    uv_mutex_lock(&lock_pause_measurement);
                    while (measurement_thread_pause != false)
                    {
                        uv_cond_wait(&cond_resume_measurement, &lock_pause_measurement);
                    }
                    uv_mutex_unlock(&lock_pause_measurement);
                    app_measurement_point((start_azimut + a) % 360, m, freq[d].frequency, val);
                    uv_sleep(500);
                    if (measurement_thread_exit)
                    {
                        /* Measurement has been canceled */
                        break;
                    }
                }
                currentTime = (double)time(NULL);
                elapsedTime = currentTime - startTime - time_paused;
                timeOneStep = elapsedTime / currentStep;
                totalTime = timeOneStep * totalSteps;
                timeLeft = totalTime - elapsedTime;
                app_progress(progress, totalTime, timeLeft);
                measured++;
                if (measurement_thread_exit)
                {
                    /* Measurement has been canceled */
                    break;
                }
            }
            /* A cancelled reversed row has gaps and cannot be written column-aligned */
            int cols = azimut_sector / resolution_azimut + 1;
            for(int e = 0; e < freq_counter; e++)
            {
                if (!reversed || measured == cols)
                {
                    fprintf(freq[e].file_stream, "%d;", m);
                    for(int i = 0; i < measured; i++)
                    {
                        fprintf(freq[e].file_stream, "%.01lf;", row[e][i]);
                    }
                }
                fputs("\n", freq[e].file_stream);
                fflush(freq[e].file_stream);
            }
            if (measurement_thread_exit)
            {
                /* Measurement has been canceled */
                break;
            }
    }
    free(row);
    scan_status = STOPPED;
    if(measurement_thread_exit == false) {
        app_message("Scan process finished.", MSG_SUCCESS);
        app_done("scan");
    }
    measurement_thread_exit = false;
    free_freq();
    /* App status is kicked via main loop */
    app_status();
    pthread_exit(0);
}