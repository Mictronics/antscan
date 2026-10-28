#include "loop.h"
#include "stdint.h"
#include "uv.h"
#include <stdbool.h>
#include <libwebsockets.h>
#include "antscan.h"
#include "time.h"
#include "head.h"
#include "receiver.h"

#include "real.h"

extern int start_f, stop_f, freq_counter;
extern char file_name[25];
extern uint16_t start_azimut, stop_azimut, n;
extern int16_t start_elev, stop_elev, resolution_elev, m, azimut_sector, resolution_azimut;
extern Frequency* freq;
time_t now;
struct tm* t;
extern uv_mutex_t lock_pause_measurement;
extern uv_cond_t cond_resume_measurement;
extern bool measurement_thread_exit, measurement_thread_pause;
extern scan_status_e scan_status;
extern struct lws_context *context;
extern double time_paused;
double totalSteps, currentStep, azimutSteps, elevSteps, progress;
double startTime, currentTime, timeOneStep, totalTime, timeLeft, elapsedTime;

void file_init()
{
    now = time(NULL);
    t = localtime(&now);
    for(int b = 0; b < freq_counter; b++)
    {
        if(file_name[1] == '0')
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
            printf("Cannot open a file");
            clean_exit(freq, freq_counter);
            return;
        }

        fprintf(freq[b].file_stream, "START_FREQ:%d;STOP_FREQ:%d;MEASURE_FREQ:%d;REF_GAIN:%lf;\n", start_f, stop_f, freq[b].frequency, freq[b].reference_gain);
        fputs("TILT/PAN;", freq[b].file_stream);
        if(azimut_sector == 360) {
            azimut_sector = 359;
        }
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
}

void measurement_loop()
{
    thread_to_core(2);
    srand(time(NULL));
    if(azimut_sector == 360) {
        azimut_sector = 359;
    }
    currentStep = 0;
    progress = 0;
    azimutSteps = (azimut_sector / resolution_azimut) + 1;
    elevSteps = (abs(start_elev - stop_elev) / resolution_elev) + 1;
    totalSteps = azimutSteps * elevSteps;
    now = time(NULL);
    t = localtime(&now);
    startTime = (t->tm_hour * 3600) + (t->tm_min * 60) + t->tm_sec;
    for(m = start_elev; m <= stop_elev; m += resolution_elev)
    {
            set_tilt_position(m);
            for(int c = 0; c < freq_counter; c++)
            {
                fprintf(freq[c].file_stream, "%d;", m);
            }
            for(n = 0; n <= azimut_sector; n += resolution_azimut)
            {
                set_pan_position(start_azimut + n);
                measure();
                currentStep += 1;
                progress = 100 * (currentStep / totalSteps);
                for(int d = 0; d < freq_counter; d++)
                {
                    #ifdef REAL
                    double val = get_data(freq[d].frequency) + freq[d].reference_gain;
                    #else
                    //double val = (rand() % 20000) / 20000.0;
                    double val = 3;
                    #endif

                    fprintf(freq[d].file_stream, "%.01lf;", val);

                    uv_mutex_lock(&lock_pause_measurement);
                    while (measurement_thread_pause != false)
                    {
                        uv_cond_wait(&cond_resume_measurement, &lock_pause_measurement);
                    }
                    uv_mutex_unlock(&lock_pause_measurement);
                    app_measurement_point((start_azimut + n) % 360, m, freq[d].frequency, val);
                    uv_sleep(500);
                    if (measurement_thread_exit)
                    {
                        /* Measurement has been canceled */
                        break;
                    }
                }
                now = time(NULL);
                t = localtime(&now);
                currentTime = (t->tm_hour * 3600) + (t->tm_min * 60) + t->tm_sec;
                elapsedTime = currentTime - startTime - time_paused;
                timeOneStep = elapsedTime / currentStep;
                totalTime = timeOneStep * totalSteps;
                timeLeft = totalTime - elapsedTime;
                app_progress(progress, totalTime, timeLeft);
                if (measurement_thread_exit)
                {
                    /* Measurement has been canceled */
                    break;
                }
            }
            for(int e = 0; e < freq_counter; e++)
            {
                fputs("\n", freq[e].file_stream);
                fflush(freq[e].file_stream);
            }
            if (measurement_thread_exit)
            {
                /* Measurement has been canceled */
                break;
            }
    }
    scan_status = STOPPED;
    if(measurement_thread_exit == false) {
        app_message("Scan process finished.", MSG_SUCCESS);
        /* Kick out app message immediately */
        lws_service(context, 0);
    }
    measurement_thread_exit = false;
    for(int f = 0; f < freq_counter; f++)
    {
        if(freq[f].file_stream != NULL) {
            fclose(freq[f].file_stream);
        }
    }
    free(freq);
    /* App status is kicked via main loop */
    app_status();
    pthread_exit(0);
}