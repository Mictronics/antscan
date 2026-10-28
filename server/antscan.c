#include "antscan.h"
#include "head.h"
#include "receiver.h"
#include "loop.h"

#include <libwebsockets.h>
#include <json-c/json.h>
#include <uv.h>
#include <string.h>
#include <getopt.h>
#include <syslog.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <limits.h>
#include <stdatomic.h>
#include <time.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include "help.h"

#include "real.h"

static error_t parse_opt(int key, char *arg, struct argp_state *state);
const char *argp_program_version = "antscan 1.0";
const char args_doc[] = "";
const char doc[] = "Antenna gain pattern measurement server.";
static struct argp argp = {options, parse_opt, args_doc, doc, NULL, NULL, NULL};

static int debug_level = 0;
static int syslog_options = LOG_PID | LOG_PERROR;
static int num_clients = 0;
static char ws_ip[20] = WS_DEFAULT_IP;
static char head_ip[20] = "192.168.1.7";
static int head_port = 8899;

/* Host header values accepted on websocket connections (anti DNS rebinding), filled in main() */
#define MAX_ALLOWED_HOSTS 8
#define MAX_HOST_LEN 64
static char extra_hosts[MAX_ALLOWED_HOSTS][MAX_HOST_LEN];
static int num_extra_hosts = 0;
static char allowed_hosts[MAX_ALLOWED_HOSTS + 3][MAX_HOST_LEN + 8];
static int num_allowed_hosts = 0;

static bool host_allowed(const char *host)
{
    for (int i = 0; i < num_allowed_hosts; i++)
    {
        if (strcasecmp(host, allowed_hosts[i]) == 0)
        {
            return true;
        }
    }
    return false;
}
static struct sockaddr_in ws_in;

struct lws_context *context;
static struct lws_context_creation_info info;
_Atomic scan_status_e scan_status = STOPPED;
uv_mutex_t lock_pause_measurement;
uv_cond_t cond_resume_measurement;
atomic_bool measurement_thread_exit = false;
bool measurement_thread_pause = false;
uv_thread_t measurement_thread;
double pause_start, time_paused = 0;

int start_f, stop_f, freq_counter = 0;
char file_name[25];
uint16_t start_azimut, stop_azimut, n;
int16_t start_elev, stop_elev, resolution_elev, m, azimut_sector, resolution_azimut;
bool serpentine = false;
Frequency* freq;

/* Outgoing messages: ring of the last MSG_QUEUE_LEN messages, each client keeps its own read position */
#define MSG_QUEUE_LEN 64
#define MSG_MAX_LEN 512

struct ws_msg
{
    unsigned char buf[LWS_PRE + MSG_MAX_LEN];
    size_t len;
};

static struct ws_msg msg_queue[MSG_QUEUE_LEN];
static unsigned long msg_count = 0; /* total messages queued, next sequence number */
static uv_mutex_t msg_lock;

/**
 * One of these is created for each client connecting.
 */
struct per_session_data
{
    unsigned long next_seq; /* next message sequence number to send */
};

/**
 * Set affinity of calling thread to specific core on a multi-core CPU
 */
int thread_to_core(int core_id)
{
    int num_cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (core_id < 0 || core_id >= num_cores)
        return EINVAL;

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);

    pthread_t current_thread = pthread_self();
    return pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
}

#define MAX_FREQUENCIES 16
#define FILENAME_MAX_LEN 24
#define FILENAME_CHARS "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-"

static bool measurement_thread_started = false;

static bool in_scan(void)
{
    return scan_status == RUNNING || scan_status == PAUSED;
}

/* Slow head/receiver operations run in their own thread, one at a time */
static atomic_bool job_busy = false;
static bool job_thread_started = false;
static uv_thread_t job_thread;
static struct
{
    server_command_e cmd;
    int val;
} job;

static void job_run(void *arg)
{
    NOTUSED(arg);
    char buffer[40];
    switch (job.cmd)
    {
    case SERVER_CMD_SET_PAN:
        if (set_pan_position(job.val) != 0)
        {
            app_message("Pan-tilt head not responding", MSG_DANGER);
            break;
        }
        snprintf(buffer, sizeof(buffer), "PAN Position set to %d°", job.val);
        app_message(buffer, MSG_SUCCESS);
        break;
    case SERVER_CMD_SET_TILT:
        if (set_tilt_position(job.val) != 0)
        {
            app_message("Pan-tilt head not responding", MSG_DANGER);
            break;
        }
        snprintf(buffer, sizeof(buffer), "TILT Position set to %d°", job.val);
        app_message(buffer, MSG_SUCCESS);
        break;
    case SERVER_CMD_CALIBRATE_PT:
        if (self_check() == 0)
        {
            app_message("Self-Check finished", MSG_SUCCESS);
        }
        else
        {
            app_message("Self-Check failed", MSG_DANGER);
        }
        app_done("check");
        break;
    case SERVER_CMD_SET_HOME:
        if (home() == 0)
        {
            app_message("Home Position", MSG_SUCCESS);
        }
        else
        {
            app_message("Pan-tilt head not responding", MSG_DANGER);
        }
        app_done("home");
        break;
    case SERVER_CMD_CALIBRATE_ANTENNA:
        if (init_receiver(start_f, stop_f) == 0)
        {
            app_message("Receiver calibrated. Change antenna", MSG_SUCCESS);
            scan_status = CALIBRATED;
        }
        else
        {
            app_message("Receiver not responding", MSG_DANGER);
            free_freq();
        }
        app_status();
        break;
    default:
        break;
    }
    job_busy = false;
}

static void start_job(server_command_e cmd, int val)
{
    if (job_thread_started)
    {
        uv_thread_join(&job_thread);
    }
    job.cmd = cmd;
    job.val = val;
    job_busy = true;
    job_thread_started = uv_thread_create(&job_thread, job_run, NULL) == 0;
    if (!job_thread_started)
    {
        job_busy = false;
        app_message("Cannot start operation", MSG_DANGER);
    }
}

static void join_measurement_thread(void)
{
    if (measurement_thread_started)
    {
        uv_thread_join(&measurement_thread);
        measurement_thread_started = false;
    }
}

/**
 * Close all data files and release the frequency list.
 */
void free_freq(void)
{
    for (int f = 0; freq != NULL && f < freq_counter; f++)
    {
        if (freq[f].file_stream != NULL)
        {
            fclose(freq[f].file_stream);
        }
    }
    free(freq);
    freq = NULL;
    freq_counter = 0;
}

static bool get_int(json_object *jarr, int idx, int lo, int hi, int *out)
{
    json_object *v = json_object_array_get_idx(jarr, idx);
    if (!json_object_is_type(v, json_type_int))
    {
        return false;
    }
    int x = json_object_get_int(v);
    if (x < lo || x > hi)
    {
        return false;
    }
    *out = x;
    return true;
}

static bool get_number(json_object *jarr, int idx, double lo, double hi, double *out)
{
    json_object *v = json_object_array_get_idx(jarr, idx);
    if (!json_object_is_type(v, json_type_int) && !json_object_is_type(v, json_type_double))
    {
        return false;
    }
    double x = json_object_get_double(v);
    if (!(x >= lo && x <= hi))
    {
        return false;
    }
    *out = x;
    return true;
}

/**
 * Validate a calibration request and, if valid, store it in the scan globals.
 * Returns NULL on success, otherwise a message for the client.
 */
static const char *parse_calibration(json_object *jarr)
{
    int fc, sector, az_res, el_start, el_stop, el_res, f_start, f_stop, f, serp = 0;
    double g;
    json_object *jname = json_object_array_get_idx(jarr, FILENAME);
    json_object *jfreq = json_object_array_get_idx(jarr, TEST_FREQUENCY);
    json_object *jgain = json_object_array_get_idx(jarr, REFERENCE_GAIN);

    if (!get_int(jarr, FREQ_COUNTER, 1, MAX_FREQUENCIES, &fc) ||
        !get_int(jarr, AZIMUTH_ANGLE, 0, 360, &sector) ||
        !get_int(jarr, AZIMUT_RESOLUTION, 1, 360, &az_res) ||
        !get_int(jarr, ELEVATION_START, -90, 90, &el_start) ||
        !get_int(jarr, ELEVATION_STOP, el_start, 90, &el_stop) ||
        !get_int(jarr, ELEVATION_RESOLUTION, 1, 180, &el_res) ||
        !get_int(jarr, START_FREQUENCY, 1, 7000, &f_start) ||
        !get_int(jarr, STOP_FREQUENCY, f_start, 7000, &f_stop) ||
        !json_object_is_type(jname, json_type_string) ||
        !json_object_is_type(jfreq, json_type_array) ||
        !json_object_is_type(jgain, json_type_array) ||
        json_object_array_length(jfreq) != (size_t)fc ||
        json_object_array_length(jgain) != (size_t)fc)
    {
        return "Wrong input";
    }
    /* Optional trailing element, old clients omit it */
    if (json_object_array_get_idx(jarr, SERPENTINE) != NULL && !get_int(jarr, SERPENTINE, 0, 1, &serp))
    {
        return "Wrong input";
    }
    const char *name = json_object_get_string(jname);
    size_t name_len = strlen(name);
    if (sector % 2 != 0 || name_len > FILENAME_MAX_LEN || strspn(name, FILENAME_CHARS) != name_len)
    {
        return "Wrong input";
    }
    for (int i = 0; i < fc; i++)
    {
        if (!get_int(jfreq, i, f_start, f_stop, &f) || !get_number(jgain, i, -100, 100, &g))
        {
            return "Wrong input";
        }
    }

    /* Valid: replace any previous calibration */
    free_freq();
    scan_status = STOPPED;
    freq = calloc(fc, sizeof(Frequency));
    if (freq == NULL)
    {
        return "Out of memory";
    }
    freq_counter = fc;
    for (int i = 0; i < fc; i++)
    {
        get_int(jfreq, i, f_start, f_stop, &freq[i].frequency);
        get_number(jgain, i, -100, 100, &freq[i].reference_gain);
    }
    /* Empty name or all zeros (web app default) selects a timestamp name */
    if (strspn(name, "0") == name_len)
    {
        name = "";
    }
    strcpy(file_name, name);
    start_azimut = 360 - (sector / 2.0);
    stop_azimut = sector / 2.0;
    azimut_sector = sector == 360 ? 359 : sector;
    resolution_azimut = az_res;
    serpentine = serp;
    start_elev = el_start;
    stop_elev = el_stop;
    resolution_elev = el_res;
    start_f = f_start;
    stop_f = f_stop;
    return NULL;
}

/**
 * Handle requests from client via broadcast callback.
 */
static void handle_client_request(void *in, size_t len)
{
    char *text = strndup(in, len);
    json_object *jarr = text ? json_tokener_parse(text) : NULL;
    free(text);
    int val, cmd;
    const char *err;
    /* We expect always an array from web application, command at index 0 */
    if (!json_object_is_type(jarr, json_type_array) || !get_int(jarr, SERVER_CMD, 0, INT_MAX, &cmd))
    {
        json_object_put(jarr);
        return;
    }
    if ((in_scan() || job_busy) &&
        (cmd == SERVER_CMD_SET_PAN || cmd == SERVER_CMD_SET_TILT || cmd == SERVER_CMD_CALIBRATE_PT ||
         cmd == SERVER_CMD_SET_HOME || cmd == SERVER_CMD_CALIBRATE_ANTENNA))
    {
        app_message("Not possible while measuring or busy", MSG_DANGER);
        json_object_put(jarr);
        return;
    }
    switch (cmd)
    {
    case SERVER_CMD_CALIBRATE_ANTENNA:
        join_measurement_thread();
        err = parse_calibration(jarr);
        if (err == NULL && file_init() != 0)
        {
            err = "Cannot open data file";
        }
        if (err != NULL)
        {
            app_message(err, MSG_DANGER);
            app_status();
            break;
        }
        start_job(SERVER_CMD_CALIBRATE_ANTENNA, 0);
        break;
    case SERVER_CMD_SCAN_START:
        /* Start scan only if reference antenna is already calibrated */
        if (job_busy)
        {
            app_message("Busy", MSG_DANGER);
            break;
        }
        if (scan_status != CALIBRATED)
        {
            app_message("Reference antenna not calibrated.", MSG_DANGER);
            break;
        }
        join_measurement_thread();
        measurement_thread_pause = false;
        measurement_thread_exit = false;
        time_paused = 0;
        scan_status = RUNNING;
        if (uv_thread_create(&measurement_thread, measurement_loop, NULL) != 0)
        {
            scan_status = CALIBRATED;
            app_message("Cannot start measurement", MSG_DANGER);
            app_status();
            break;
        }
        measurement_thread_started = true;
        app_message("Measurement started", MSG_SUCCESS);
        app_status();
        break;
    case SERVER_CMD_SCAN_PAUSE:
        if (scan_status == RUNNING)
        {
            uv_mutex_lock(&lock_pause_measurement);
            measurement_thread_pause = true;
            uv_mutex_unlock(&lock_pause_measurement);
            pause_start = (double)time(NULL);
            app_message("Measurement paused", MSG_WARNING);
            scan_status = PAUSED;
            app_status();
        }
        else if (scan_status == PAUSED)
        {
            uv_mutex_lock(&lock_pause_measurement);
            measurement_thread_pause = false;
            uv_cond_broadcast(&cond_resume_measurement);
            uv_mutex_unlock(&lock_pause_measurement);
            time_paused += (double)time(NULL) - pause_start;
            app_message("Measurement continued", MSG_SUCCESS);
            scan_status = RUNNING;
            app_status();
        }
        break;
    case SERVER_CMD_SCAN_STOP:
        if (!in_scan())
        {
            app_message("No measurement running", MSG_WARNING);
            break;
        }
        uv_mutex_lock(&lock_pause_measurement);
        measurement_thread_exit = true;
        measurement_thread_pause = false;
        uv_cond_broadcast(&cond_resume_measurement);
        uv_mutex_unlock(&lock_pause_measurement);
        join_measurement_thread();
        app_message("Measurement stopped", MSG_DANGER);
        app_status();
        break;
    case SERVER_CMD_SET_PAN:
    case SERVER_CMD_SET_TILT:
        if (get_int(jarr, 1, cmd == SERVER_CMD_SET_PAN ? 0 : -90, cmd == SERVER_CMD_SET_PAN ? 360 : 90, &val))
        {
            start_job(cmd, val);
        }
        else
        {
            app_message("Wrong input", MSG_DANGER);
        }
        break;
    case SERVER_CMD_CALIBRATE_PT:
    case SERVER_CMD_SET_HOME:
        start_job(cmd, 0);
        break;
    default:
        break;
    }
    json_object_put(jarr);
}

/**
 * Queue a json object for all clients and release it. Safe to call from any thread.
 */
static void ws_send(json_object *jobj)
{
    size_t len = 0;
    const char *p = json_object_to_json_string_length(jobj, JSON_C_TO_STRING_PLAIN, &len);
    if (len <= MSG_MAX_LEN)
    {
        uv_mutex_lock(&msg_lock);
        struct ws_msg *msg = &msg_queue[msg_count % MSG_QUEUE_LEN];
        memcpy(&msg->buf[LWS_PRE], p, len);
        msg->len = len;
        msg_count++;
        uv_mutex_unlock(&msg_lock);
        /* Wakes the service thread, which then writes to all clients */
        lws_cancel_service(context);
    }
    json_object_put(jobj);
}

/**
 * Broadcast callback for websocket protocol.
 */
static int callback_broadcast(struct lws *wsi, enum lws_callback_reasons reason,
                              void *user, void *in, size_t len)
{
    struct per_session_data *pss = (struct per_session_data *)user;

    switch (reason)
    {
    case LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION:
    {
        /* Host must be one we serve (DNS rebinding), and a browser's Origin must be that same host
         * (cross-site websocket). Origin may be absent only for non-browser clients. */
        char origin[128], host[MAX_HOST_LEN + 8];
        if (lws_hdr_copy(wsi, host, sizeof(host), WSI_TOKEN_HOST) <= 0 || !host_allowed(host))
        {
            lwsl_notice("Rejected websocket for unknown Host\n");
            return -1;
        }
        if (lws_hdr_total_length(wsi, WSI_TOKEN_ORIGIN) > 0)
        {
            if (lws_hdr_copy(wsi, origin, sizeof(origin), WSI_TOKEN_ORIGIN) <= 0)
            {
                return -1;
            }
            const char *origin_host = strstr(origin, "://");
            origin_host = origin_host ? origin_host + 3 : origin;
            if (strcasecmp(origin_host, host) != 0)
            {
                lwsl_notice("Rejected websocket from foreign origin %s\n", origin);
                return -1;
            }
        }
        /* Limit number of connected clients */
        if (num_clients >= MAX_CLIENTS)
        {
            lwsl_notice("%d clients already connected. New connection rejected...\n", MAX_CLIENTS);
            return -1;
        }
        break;
    }

    case LWS_CALLBACK_ESTABLISHED:
        ++num_clients;
        lwsl_notice("Client connected.\n");
        uv_mutex_lock(&msg_lock);
        pss->next_seq = msg_count;
        uv_mutex_unlock(&msg_lock);
        /* Tell the new client where we are */
        app_status();
        break;

    case LWS_CALLBACK_CLOSED:
        --num_clients;
        lwsl_notice("Client disconnected.\n");
        break;

    case LWS_CALLBACK_EVENT_WAIT_CANCELLED:
        lws_callback_on_writable_all_protocol(context, lws_get_protocol(wsi));
        break;

    case LWS_CALLBACK_SERVER_WRITEABLE:
    {
        int m = 0;
        bool more = false;
        uv_mutex_lock(&msg_lock);
        /* Skip messages that were overwritten while this client was slow */
        if (msg_count - pss->next_seq > MSG_QUEUE_LEN)
        {
            pss->next_seq = msg_count - MSG_QUEUE_LEN;
        }
        if (pss->next_seq < msg_count)
        {
            struct ws_msg *msg = &msg_queue[pss->next_seq % MSG_QUEUE_LEN];
            m = lws_write(wsi, &msg->buf[LWS_PRE], msg->len, LWS_WRITE_TEXT);
            if (m >= (int)msg->len)
            {
                pss->next_seq++;
            }
            more = pss->next_seq < msg_count;
        }
        uv_mutex_unlock(&msg_lock);
        if (m < 0)
        {
            lwsl_err("ERROR %d writing to ws socket\n", m);
            return -1;
        }
        if (more)
        {
            lws_callback_on_writable(wsi);
        }
        break;
    }

    case LWS_CALLBACK_RECEIVE:
        if (len > 0)
        {
            handle_client_request(in, len);
        }
        break;

    default:
        break;
    }

    return 0;
}

/**
 * Serve the web application from ./client on the same port as the websocket.
 */
static const struct lws_http_mount client_mount = {
    .mountpoint = "/",
    .origin = "./client",
    .def = "index.html",
    .origin_protocol = LWSMPRO_FILE,
    .mountpoint_len = 1,
};

/**
 * Websocket protocol definition.
 */
static struct lws_protocols protocols[] = {
    {"http",
     lws_callback_http_dummy,
     0,
     0,
     0,
     NULL,
     0},
    {"broadcast",
     callback_broadcast,
     sizeof(struct per_session_data),
     WSBUFFERSIZE,
     0, NULL, 0},
    {NULL, NULL, 0, 0, 0, NULL, 0} /* terminator */
};

static void ws_send_cmd(const char *cmd, json_object *data)
{
    json_object *jobj = json_object_new_object();
    json_object_object_add(jobj, "cmd", json_object_new_string(cmd));
    json_object_object_add(jobj, "data", data);
    ws_send(jobj);
}

/**
 * Forward status to web application.
 */
void app_status()
{
    ws_send_cmd("status", json_object_new_int(scan_status));
}

/**
 * Tell web application that a long running operation ("scan", "home", "check") ended.
 */
void app_done(const char *what)
{
    ws_send_cmd("done", json_object_new_string(what));
}

/**
 * Forward measurement point to web application.
 */
void app_measurement_point(int az, int el, int freq, double val)
{
    json_object *jarr = json_object_new_array();
    json_object_array_add(jarr, json_object_new_int(az));
    json_object_array_add(jarr, json_object_new_int(el));
    json_object_array_add(jarr, json_object_new_int(freq));
    json_object_array_add(jarr, json_object_new_double(val));
    ws_send_cmd("point", jarr);
}

void app_progress(double progress, double totalTime, double timeLeft)
{
    json_object *jarr = json_object_new_array();
    json_object_array_add(jarr, json_object_new_int(progress));
    json_object_array_add(jarr, json_object_new_int(totalTime));
    json_object_array_add(jarr, json_object_new_int(timeLeft));
    ws_send_cmd("progress", jarr);
}

/**
 * Forward message to web application.
 */
void app_message(const char *msg, app_msg_type_e type)
{
    /* Indexed by app_msg_type_e */
    static const char *const cmd[] = {"primary", "secondary", "success", "info",
                                      "warning", "danger", "light", "dark"};
    ws_send_cmd(cmd[type], json_object_new_string(msg));
}

/**
 * Parse command line options, if any.
 *
 * run ./example --help on command line for output
 */
static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    switch (key)
    {
    case OPT_WS_IP:
        if (inet_pton(AF_INET, arg, &ws_in.sin_addr) == 0)
        {
            lwsl_err("Error in websocket server IP.\n");
        }
        else
        {
            /* Overwrite default IP string*/
            inet_ntop(AF_INET, &ws_in.sin_addr, ws_ip, sizeof ws_ip);
        }
        break;
    case OPT_WS_PORT:
    case OPT_HEAD_PORT:
    {
        int port = atoi(arg);
        if (port < 1 || port > 65535)
        {
            argp_error(state, "Invalid port '%s'", arg);
        }
        if (key == OPT_WS_PORT)
        {
            ws_in.sin_port = port;
        }
        else
        {
            head_port = port;
        }
        break;
    }
    case OPT_HEAD_IP:
    {
        struct in_addr addr;
        if (inet_pton(AF_INET, arg, &addr) != 1)
        {
            argp_error(state, "Invalid IP '%s'", arg);
        }
        snprintf(head_ip, sizeof(head_ip), "%s", arg);
        break;
    }
    case OPT_ALLOW_HOST:
        if (strlen(arg) >= MAX_HOST_LEN || num_extra_hosts >= MAX_ALLOWED_HOSTS)
        {
            argp_error(state, "Invalid or too many --allow-host");
        }
        strcpy(extra_hosts[num_extra_hosts++], arg);
        break;
    case OPT_RECEIVER:
        if (strlen(arg) >= sizeof(receiver_resource))
        {
            argp_error(state, "VISA resource too long");
        }
        strcpy(receiver_resource, arg);
        break;
    case ARGP_KEY_END:
        if (state->arg_num > 0)
        {
            /* We use only options but no arguments */
            argp_usage(state);
        }
        break;
    default:
        return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

/**
 * Incoming signal handler. Close server nice and clean.
 */
static void sighandler(int sig)
{
    NOTUSED(sig);
    /* Stop websocket server */
    lws_cancel_service(context);
    lws_context_destroy(context);
    exit(EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
    clean();
    uv_mutex_init(&msg_lock);
    uv_mutex_init(&lock_pause_measurement);
    uv_cond_init(&cond_resume_measurement);

    /* Set default IP addresses and ports for all service. */
    inet_pton(AF_INET, ws_ip, &ws_in.sin_addr);
    ws_in.sin_port = WS_DEFAULT_PORT;

    /* Parse the command line options */
    if (argp_parse(&argp, argc, argv, 0, 0, 0))
    {
        exit(EXIT_SUCCESS);
    }

    const char *own_hosts[] = {"localhost", "127.0.0.1", ws_ip};
    for (size_t i = 0; i < sizeof(own_hosts) / sizeof(own_hosts[0]); i++)
    {
        snprintf(allowed_hosts[num_allowed_hosts++], sizeof(allowed_hosts[0]), "%s:%d", own_hosts[i], ws_in.sin_port);
    }
    for (int i = 0; i < num_extra_hosts; i++)
    {
        snprintf(allowed_hosts[num_allowed_hosts++], sizeof(allowed_hosts[0]), "%s:%d", extra_hosts[i], ws_in.sin_port);
    }

    if(create_socket() == -1)
    {
        return EXIT_FAILURE;
    }

    // Send connection request to server
    #ifdef REAL
    if (connect_server(head_ip, head_port) == -1)
    {
        close_socket();
        return EXIT_FAILURE;
    }
    #endif

    /* Take care to zero down the info struct, contains random garbage
     * from the stack otherwise.
     */
    memset(&info, 0, sizeof(info));
    /* Fill in defaults */
    info.port = ws_in.sin_port;
    info.iface = ws_ip;
    info.protocols = protocols;
    info.mounts = &client_mount;
    info.gid = -1;
    info.uid = -1;
    info.max_http_header_pool = 16;
    info.options = 0;
    info.extensions = NULL;
    info.timeout_secs = 5;
    info.max_http_header_data = 2048;

    /* Catch signal for program termination */
    signal(SIGINT, sighandler);
    signal(SIGTERM, sighandler);
    signal(SIGQUIT, sighandler);

    /* Only try to log things according to our debug_level (in libwebsocket)*/
    setlogmask(LOG_UPTO(LOG_DEBUG));
    openlog("lwsts", syslog_options, LOG_DAEMON);

    /* Tell the library what debug level to emit and to send it to syslog */
    lws_set_log_level(debug_level, lwsl_emit_syslog);

    info.max_http_header_pool = 16;
    info.timeout_secs = 5;

    /* Create libwebsocket context representing this server */
    context = lws_create_context(&info);
    if (context == NULL)
    {
        lwsl_err("libwebsocket init failed\n");
        return EXIT_FAILURE;
    }

    // Infinite loop, to end this server send SIGTERM. (CTRL+C) */
    for (;;)
    {
        lws_service(context, 100);
        /* libwebsocket_service will process all waiting events with
         * their callback functions and then wait 100 ms.
         * (This is a single threaded web server and this will keep our
         * server from generating load while there are no
         * requests to process)
         */
    }

    return 0;
}