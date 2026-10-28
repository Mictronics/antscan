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
static struct sockaddr_in ws_in;

struct lws_context *context;
static struct lws_context_creation_info info;
static unsigned char wsbuffer[WSBUFFERSIZE];
static unsigned char *pwsbuffer = wsbuffer;
static int wsbuffer_len = 0;
scan_status_e scan_status;
uv_mutex_t lock_pause_measurement;
uv_cond_t cond_resume_measurement;
bool measurement_thread_exit = false;
bool measurement_thread_pause = false;
uv_thread_t measurement_thread;
time_t now2;
struct tm* t2;
double pause_start, pause_stop, time_paused = 0;

int start_f, stop_f, freq_counter = 0;
char file_name[25];
uint16_t start_azimut, stop_azimut, n;
int16_t start_elev, stop_elev, resolution_elev, m, azimut_sector, resolution_azimut;
Frequency* freq;

/**
 * One of these is created for each client connecting.
 */
struct per_session_data
{
    struct per_session_data *pss_list;
    struct lws *wsi;
};

/**
 *  One of these is created for each vhost our protocol is used with.
 */
struct per_vhost_data
{
    struct lws_context *context;
    struct lws_vhost *vhost;
    const struct lws_protocols *protocol;
    struct per_session_data *pss_list; // linked-list of live pss
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

/**
 * Parse json array and output content
 */
static void json_parse_array(json_object *jarray, jarray_index_e key)
{
    json_object *jvalue;
     for(int j = 0; j < freq_counter; j++) {
        jvalue = json_object_array_get_idx(jarray, j);
        if(key == TEST_FREQUENCY) {
            freq[j].frequency = json_object_get_int(jvalue);
        } else if(key == REFERENCE_GAIN) {
            freq[j].reference_gain = json_object_get_double(jvalue);
        }
    }
}

/**
 * Handle requests from client via broadcast callback.
 */
static void handle_client_request(void *in, size_t len)
{
    NOTUSED(len);
    /* Parse json string */
    json_object *jarr = json_tokener_parse(in);
    enum json_type type;
    json_object *jvalue;
    enum server_command_e cmd = SERVER_CMD_NULL;
    int val;
    char buffer[25];
    /* We expect always an array from web application */
    /* Get server command at index 0 */
    jvalue = json_object_array_get_idx(jarr, SERVER_CMD);
    type = json_object_get_type(jvalue);
    if (type == json_type_int)
    {
        cmd = json_object_get_int(jvalue);
        switch (cmd)
        {
        case SERVER_CMD_CALIBRATE_ANTENNA: 
            jvalue = json_object_array_get_idx(jarr, FREQ_COUNTER);
            freq_counter = (int)json_object_get_int(jvalue);
            freq = (Frequency*) malloc(freq_counter * sizeof(Frequency));
            if(freq == NULL)
            {
                app_message("Malloc error", MSG_DANGER);
                lws_service(context, 0);
                clean_exit(freq, freq_counter);
                exit(EXIT_FAILURE);
            }

            jvalue = json_object_array_get_idx(jarr, FILENAME);
            strcpy(file_name, json_object_get_string(jvalue));
            jvalue = json_object_array_get_idx(jarr, AZIMUTH_ANGLE);
            azimut_sector = (int)json_object_get_int(jvalue);
            jvalue = json_object_array_get_idx(jarr, AZIMUT_RESOLUTION);
            resolution_azimut = (int)json_object_get_int(jvalue);
            jvalue = json_object_array_get_idx(jarr, ELEVATION_START);
            start_elev = (int)json_object_get_int(jvalue);
            jvalue = json_object_array_get_idx(jarr, ELEVATION_STOP);
            stop_elev = (int)json_object_get_int(jvalue);
            jvalue = json_object_array_get_idx(jarr, ELEVATION_RESOLUTION);
            resolution_elev = (int)json_object_get_int(jvalue);
            jvalue = json_object_array_get_idx(jarr, START_FREQUENCY);
            start_f = (int)json_object_get_int(jvalue);
            jvalue = json_object_array_get_idx(jarr, STOP_FREQUENCY);
            stop_f = (int)json_object_get_int(jvalue);
            jvalue = json_object_array_get_idx(jarr, TEST_FREQUENCY);
            json_parse_array(jvalue, TEST_FREQUENCY);
            jvalue = json_object_array_get_idx(jarr, REFERENCE_GAIN);
            json_parse_array(jvalue, REFERENCE_GAIN);

            start_azimut = 360 - (azimut_sector / 2.0);
            stop_azimut = azimut_sector / 2.0;

            init_receiver(start_f, stop_f);
            file_init();
            app_message("Receiver calibrated. Change antenna", MSG_SUCCESS);
            lws_service(context, 0);
            scan_status = CALIBRATED;
            app_status();
            break;
        case SERVER_CMD_SCAN_START: //start measurement loop
            /* Start scan only if reference antenna is already calibrated */
            if (scan_status == CALIBRATED)
            {
                measurement_thread_pause = false;
                uv_thread_create(&measurement_thread, measurement_loop, NULL);
            }
            else
            {
                app_message("Reference antenna not calibrated.", MSG_DANGER);
                lws_service(context, 0);
            }
            app_message("Measurement started", MSG_SUCCESS);
            lws_service(context, 0);
            scan_status = RUNNING;
            app_status();
            break;
        case SERVER_CMD_SCAN_PAUSE: //pause measurement loop
            if (scan_status == RUNNING)
            {
                /* Measurement running, try pause */
                uv_mutex_lock(&lock_pause_measurement);
                measurement_thread_pause = true;
                uv_mutex_unlock(&lock_pause_measurement);
                now2 = time(NULL);
                t2 = localtime(&now2);
                pause_start = (t2->tm_hour * 3600) + (t2->tm_min * 60) + t2->tm_sec;
                app_message("Measurement paused", MSG_WARNING);
                lws_service(context, 0);
                scan_status = PAUSED;
                app_status();
            }
            else
            {
                /* Measurement paused, try running */
                uv_mutex_lock(&lock_pause_measurement);
                measurement_thread_pause = false;
                uv_cond_broadcast(&cond_resume_measurement);
                uv_mutex_unlock(&lock_pause_measurement);
                now2 = time(NULL);
                t2 = localtime(&now2);
                pause_stop = (t2->tm_hour * 3600) + (t2->tm_min * 60) + t2->tm_sec;
                time_paused += pause_stop - pause_start;
                app_message("Measurement continued", MSG_SUCCESS);
                lws_service(context, 0);
                scan_status = RUNNING;
                app_status();
            }
            break;
        case SERVER_CMD_SCAN_STOP: //stop measurement loop
            uv_mutex_lock(&lock_pause_measurement);
            measurement_thread_exit = true;
            measurement_thread_pause = false;
            uv_cond_broadcast(&cond_resume_measurement);
            uv_mutex_unlock(&lock_pause_measurement);
            uv_thread_join(&measurement_thread);
            app_message("Measurement stoped", MSG_DANGER);
            lws_service(context, 0);
            app_status();
            break;
        case SERVER_CMD_SET_PAN: //set pan position
            jvalue = json_object_array_get_idx(jarr, 1);
            type = json_object_get_type(jvalue);
            if(type == json_type_int) {
                val = (int)json_object_get_int(jvalue);
                set_pan_position(val);
                sprintf(buffer, "PAN Position set to %d°", val);
                app_message(buffer, MSG_SUCCESS);
            } else {
                app_message("Wrong input", MSG_DANGER);
            }
            break;
        case SERVER_CMD_SET_TILT: //set tilt position
            jvalue = json_object_array_get_idx(jarr, 1);
            type = json_object_get_type(jvalue);
            if(type == json_type_int) {
                val = (int)json_object_get_int(jvalue);
                set_tilt_position(val);
                sprintf(buffer, "TILT Position set to %d°", val);
                app_message(buffer, MSG_SUCCESS);
            } else {
                app_message("Wrong input", MSG_DANGER);
            }
            break;
        case SERVER_CMD_CALIBRATE_PT: //self-check
            self_check();
            app_message("Self-Check finished", MSG_SUCCESS);
            break;
        case SERVER_CMD_SET_HOME: //home
            home();
            app_message("Home Position", MSG_SUCCESS);
            break;
        default:
            break;
        }
    }
}

/**
 * Broadcast callback for websocket protocol.
 */
static int callback_broadcast(struct lws *wsi, enum lws_callback_reasons reason,
                              void *user, void *in, size_t len)
{
    struct per_session_data *pss = (struct per_session_data *)user;
    struct per_vhost_data *vhd = (struct per_vhost_data *)lws_protocol_vh_priv_get(lws_get_vhost(wsi), lws_get_protocol(wsi));
    int m = 0;

    switch (reason)
    {
    case LWS_CALLBACK_FILTER_NETWORK_CONNECTION:
        /* Limit number of connected clients */
        if (num_clients > 3)
        {
            lwsl_notice("3 clients already connected. New connection rejected...\n");
            return -1;
        }
        break;

    case LWS_CALLBACK_PROTOCOL_INIT:
        vhd = lws_protocol_vh_priv_zalloc(lws_get_vhost(wsi), lws_get_protocol(wsi), sizeof(struct per_vhost_data));
        vhd->context = lws_get_context(wsi);
        vhd->protocol = lws_get_protocol(wsi);
        vhd->vhost = lws_get_vhost(wsi);
        break;

    case LWS_CALLBACK_PROTOCOL_DESTROY:
        break;

    case LWS_CALLBACK_ESTABLISHED:
        ++num_clients;
        lwsl_notice("Client connected.\n");
        pss->wsi = wsi;
        lws_ll_fwd_insert(pss, pss_list, vhd->pss_list);
        break;

    case LWS_CALLBACK_CLOSED:
    case LWS_CALLBACK_WSI_DESTROY:
        --num_clients;
        lwsl_notice("Client disconnected.\n");
        /* remove our closing pss from the list of live pss */
        lws_ll_fwd_remove(struct per_session_data, pss_list, pss, vhd->pss_list);
        break;

    case LWS_CALLBACK_SERVER_WRITEABLE:
        /* notice we allowed for LWS_PRE in the payload already */
        m = lws_write(wsi, &pwsbuffer[LWS_SEND_BUFFER_PRE_PADDING], wsbuffer_len, LWS_WRITE_TEXT);
        if (m < wsbuffer_len)
        {
            lwsl_err("ERROR %d writing to ws socket\n", m);
            return -1;
        }
        break;

    case LWS_CALLBACK_RECEIVE:
        if (!vhd->pss_list || len <= 0)
        {
            break;
        }
        /* Reply empty json object on unknown request */
        memcpy(&pwsbuffer[LWS_SEND_BUFFER_PRE_PADDING], "\"{}\"", 4);
        wsbuffer_len = 4;
        handle_client_request(in, len);
        /*
         * let every subscriber know we want to write something
         * on them as soon as they are ready
         */
        lws_start_foreach_llp(struct per_session_data **, ppss, vhd->pss_list)
        {
            lws_callback_on_writable((*ppss)->wsi);
        }
        lws_end_foreach_llp(ppss, pss_list);
        break;

    default:
        break;
    }

    return 0;
}

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

/**
 * Serialize json object into the websocket buffer and queue it for all clients.
 * Takes ownership of jobj.
 */
static void ws_send(json_object *jobj)
{
    size_t len = 0;
    const char *p = json_object_to_json_string_length(jobj, JSON_C_TO_STRING_PLAIN, &len);
    memcpy(&pwsbuffer[LWS_SEND_BUFFER_PRE_PADDING], p, len);
    wsbuffer_len = len;
    lws_callback_on_writable_all_protocol(context, &protocols[1]);
    json_object_put(jobj);
}

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
    /* Send measurement point immediately to web application */
    lws_service(context, 0);
}

void app_progress(double progress, double totalTime, double timeLeft)
{
    json_object *jarr = json_object_new_array();
    json_object_array_add(jarr, json_object_new_int(progress));
    json_object_array_add(jarr, json_object_new_int(totalTime));
    json_object_array_add(jarr, json_object_new_int(timeLeft));
    ws_send_cmd("progress", jarr);
    lws_service(context, 0);
}

/**
 * Forward message to web application. Sent as soon as lws_service runs;
 * callers needing it immediately call lws_service themselves.
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
        ws_in.sin_port = atoi(arg);
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

void clean_exit(Frequency* freq, int freq_counter)
{
    close_socket();

    for(int f = 0; f < freq_counter; f++)
    {
        if(freq[f].file_stream != NULL) {
            fclose(freq[f].file_stream);
        }
    }

    free(freq);
}

int main(int argc, char **argv)
{
    clean();

    memset(file_name, '0', sizeof(file_name));

    if(create_socket() == -1)
    {
        return 0;
    }

    // Send connection request to server
    #ifdef REAL
    if (connect_server() == -1)
    {
        close_socket();
        return 0;
    }
    #endif

    /* Set default IP addresses and ports for all service. */
    inet_pton(AF_INET, ws_ip, &ws_in.sin_addr);
    ws_in.sin_port = WS_DEFAULT_PORT;

    /* Parse the command line options */
    if (argp_parse(&argp, argc, argv, 0, 0, 0))
    {
        exit(EXIT_SUCCESS);
    }

    /* Take care to zero down the info struct, contains random garbage
     * from the stack otherwise.
     */
    memset(&info, 0, sizeof(info));
    /* Fill in defaults */
    info.port = ws_in.sin_port;
    info.iface = ws_ip;
    info.protocols = protocols;
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

    sighandler(0);

    clean_exit(freq, freq_counter);

    return 0;
}