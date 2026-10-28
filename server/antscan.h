#include "stdio.h"
#include <json-c/json.h>

#define NOTUSED(V) ((void)V)
#define WSBUFFERSIZE 8192 /* Byte */
#define MAX_CLIENTS 4

#define WS_DEFAULT_IP "127.0.0.1"
#define WS_DEFAULT_PORT 10024

typedef enum jarray_index_e
{
    SERVER_CMD = 0,
    FILENAME,
    AZIMUTH_ANGLE,
    AZIMUT_RESOLUTION,
    ELEVATION_START,
    ELEVATION_STOP,
    ELEVATION_RESOLUTION,
    START_FREQUENCY,
    STOP_FREQUENCY,
    TEST_FREQUENCY,
    REFERENCE_GAIN,
    FREQ_COUNTER,
    SERPENTINE
} jarray_index_e;

typedef enum scan_status_e
{
    STOPPED = 1,
    RUNNING = 2,
    PAUSED = 3,
    CALIBRATED = 4
} scan_status_e;

typedef enum server_command_e
{
    SERVER_CMD_NULL = 0,
    SERVER_CMD_SCAN_START,
    SERVER_CMD_SCAN_STOP,
    SERVER_CMD_SCAN_PAUSE,
    SERVER_CMD_CALIBRATE_PT,
    SERVER_CMD_SET_PAN,
    SERVER_CMD_SET_TILT,
    SERVER_CMD_SET_HOME,
    SERVER_CMD_CALIBRATE_ANTENNA,
    SERVER_CMD_STATUS,
    SERVER_CMD_MEASUREMENT_POINT
} server_command_e;

typedef enum app_msg_type_e
{
    MSG_PRIMARY = 0,
    MSG_SECONDARY,
    MSG_SUCCESS,
    MSG_INFO,
    MSG_WARNING,
    MSG_DANGER,
    MSG_LIGHT,
    MSG_DARK
} app_msg_type_e;

typedef struct Frequencies
{
    int frequency;
    char file_path[FILENAME_MAX];
    FILE* file_stream;
    double reference_gain;
} Frequency;

void free_freq(void);
void app_message(const char *msg, app_msg_type_e type);
void app_status();
void app_version();
void app_done(const char *what);
void app_measurement_point(int az, int el, int freq, double val);
void app_progress(double progress, double totalTime, double timeLeft);
int thread_to_core(int core_id);