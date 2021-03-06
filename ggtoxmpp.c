#include <stdlib.h>
#include <libgadu.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <strophe.h>
#include <signal.h>
#include <libconfig.h>

#define KA_TIMEOUT 60
#define KA_INTERVAL 1

struct gg_session *session;
struct gg_login_params params;
struct gg_event *event;
struct gg_event_msg msg;

xmpp_conn_t *conn;
pthread_t gg_ping_tid, xmpp_loop_tid;
xmpp_ctx_t *ctx;
const char *xmpp_target;
int running = 1;
gg_pubdir50_t query;

struct buddy {
    int number;
    char *name;
} buddy_list[128];
int buddy_size = 0;

void buddy_add(int number, char *name) {
    struct buddy *b = &buddy_list[buddy_size++];
    b->number = number;
    b->name = name;
}

char *buddy_name(int number) {
    for(int i = 0; i < buddy_size; i++) {
        if(buddy_list[i].number == number) {
            return buddy_list[i].name;
        }
    }

    query = gg_pubdir50_new(GG_PUBDIR50_SEARCH_REQUEST);
    char *sender_string = malloc(10);
    sprintf(sender_string, "%i", event->event.msg.sender);
    gg_pubdir50_add(query, GG_PUBDIR50_UIN, sender_string);
    gg_pubdir50(session, query);
    return NULL;
}

void quit() {
    printf("\nStopping ggtoxmpp...");
    running = 0;
    gg_logoff(session);
    xmpp_stop(ctx);
}

void sigint_handler(int dummy) {
    quit();
}

void *gg_ping_loop(void *arg) {
    while(running) {
        sleep(60);
        gg_ping(session);
    }

    return 0;
}

void *xmpp_loop(void *arg) {
    xmpp_run(ctx);
}

void xmpp_send_simple_message(xmpp_conn_t *conn, const char const * to,
                              const char const *message) {
   xmpp_stanza_t *msg, *body, *text;
   xmpp_ctx_t *ctx = xmpp_conn_get_context(conn);

   msg = xmpp_stanza_new(ctx);
   xmpp_stanza_set_name(msg, "message");
   xmpp_stanza_set_type(msg, "chat");
   xmpp_stanza_set_attribute(msg, "to", to);

   body = xmpp_stanza_new(ctx);
   xmpp_stanza_set_name(body, "body");

   text = xmpp_stanza_new(ctx);
   xmpp_stanza_set_text(text, message);
   xmpp_stanza_add_child(body, text);
   xmpp_stanza_add_child(msg, body);

   xmpp_send(conn, msg);
   xmpp_stanza_release(msg);
}

void conn_handler(xmpp_conn_t *const conn, const xmpp_conn_event_t status,
                  const int error, xmpp_stream_error_t *const stream_error,
                  void *const userdata) {
    xmpp_ctx_t *ctx = (xmpp_ctx_t *)userdata;
    int secured;

    if(status == XMPP_CONN_CONNECT) {
        fprintf(stderr, "XMPP: connected\n");
        secured = xmpp_conn_is_secured(conn);
        fprintf(stderr, "XMPP: connection is %s.\n",
                secured ? "secured" : "NOT secured");
        xmpp_stanza_t *pres = xmpp_presence_new(ctx);
        xmpp_send(conn, pres);
        xmpp_stanza_release(pres);
        xmpp_send_simple_message(conn, xmpp_target, "Connection estabilished");
    }
    else {
        fprintf(stderr, "XMPP: disconnected\n");
        xmpp_stop(ctx);
    }
}

int main(void) {
    //Register SIGINT handler
    signal(SIGINT, sigint_handler);

    //Read configuration
    const char *xmpp_jid, *xmpp_pass, *gg_pass, *gg_reply;
    char *time_string = malloc(16);
    int gg_id;
    config_t cfg;
    config_init(&cfg);

    if(!config_read_file(&cfg, "./ggtoxmpp.conf")) {
        fprintf(stderr, "%s:%d - %s\n", config_error_file(&cfg),
            config_error_line(&cfg), config_error_text(&cfg));
        config_destroy(&cfg);
        return(EXIT_FAILURE);
    }

    config_lookup_string(&cfg, "xmpp.jid", &xmpp_jid);
    config_lookup_string(&cfg, "xmpp.password", &xmpp_pass);
    config_lookup_string(&cfg, "xmpp.target", &xmpp_target);
    config_lookup_int(&cfg, "gg.id", &gg_id);
    config_lookup_string(&cfg, "gg.password", &gg_pass);
    config_lookup_string(&cfg, "gg.reply", &gg_reply);

    //libstrophe init
    char *reply_text = (char*)malloc(128);
    long flags = 0;
    int tcp_keepalive = 1;
    xmpp_initialize();
    ctx = xmpp_ctx_new(NULL, NULL);
    conn = xmpp_conn_new(ctx);
    xmpp_conn_set_flags(conn, flags);
    xmpp_conn_set_keepalive(conn, KA_TIMEOUT, KA_INTERVAL);
    xmpp_conn_set_jid(conn, xmpp_jid);
    xmpp_conn_set_pass(conn, xmpp_pass);
    xmpp_connect_client(conn, NULL, 0, conn_handler, ctx);
    pthread_create(&xmpp_loop_tid, NULL, &xmpp_loop, NULL);

    //libgadu init
    memset(&params, 0, sizeof(params));
    params.uin = gg_id;
    params.password = (char *) gg_pass;
    params.async = 0;
    params.status = GG_STATUS_INVISIBLE;
    params.encoding = GG_ENCODING_UTF8;
    session = gg_login(&params);

    if(!session) {
        printf("Could not connect\n");
        running = 0;
        exit(EXIT_FAILURE);
    }

    printf("Connected!\n");
    pthread_create(&gg_ping_tid, NULL, &gg_ping_loop, NULL);

    while(event = gg_watch_fd(session)) {
        if(event->type == GG_EVENT_MSG) {
            msg = (event->event).msg;
            strftime(time_string, 16, "%d-%m-%y %H:%M", gmtime(&msg.time));
            char *sender_name_optional = buddy_name(msg.sender);
            char *sender_name;

            if(sender_name_optional == NULL) {
                sender_name = "";
            }
            else {
                sender_name = malloc(strlen(sender_name_optional) + 4);
                sprintf(sender_name, " (%s)", sender_name_optional);
            }

            printf("%s: %i%s\n", time_string, msg.sender, sender_name);
            sprintf(reply_text, "%s: %i%s\n%s", time_string, msg.sender,
                    sender_name, msg.message);
            xmpp_send_simple_message(conn, xmpp_target, reply_text);
            gg_send_message(session, GG_CLASS_MSG, msg.sender, gg_reply);
        }
        else if(event->type == GG_EVENT_PUBDIR50_SEARCH_REPLY) {
            gg_pubdir50_t result = event->event.pubdir50;

            /* buddy_add(atoi(gg_pubdir50_get(result, 0, GG_PUBDIR50_UIN)), */
            /* buddy_add(, */
            const char *name = gg_pubdir50_get(result, 0, GG_PUBDIR50_NICKNAME);
            /* printf("%s", gg_pubdir50_get(result, 0, GG_PUBDIR50_FAMILYNAME)); */

            if(name == NULL) {
                name = malloc(64);
                sprintf((char *) name, "%s",
                        gg_pubdir50_get(result, 0, GG_PUBDIR50_FIRSTNAME));

                const char *last_name = gg_pubdir50_get(result, 0,
                                                        GG_PUBDIR50_LASTNAME);
                if(last_name != NULL) {
                    sprintf((char *) name, "%s %s", name, last_name);
                }
            }

            buddy_add(atoi(gg_pubdir50_get(result, 0, GG_PUBDIR50_UIN)),
                      (char *) name);
            gg_pubdir50_free(query);
        }

        gg_event_free(event);
    }

    // release our connection and context
    xmpp_conn_release(conn);
    xmpp_ctx_free(ctx);
    gg_free_session(session);
    xmpp_shutdown();
    config_destroy(&cfg);
}
