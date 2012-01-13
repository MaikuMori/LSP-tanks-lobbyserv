#include <sys/types.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <err.h>

#include <event2/event.h>
#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>

#include <iniparser.h>

#ifdef HAVE_CONFIG_H
    #include <config.h>
#endif

#ifndef CONF_FILE
    #define CONF_FILE "./lts.conf"
#endif
#ifndef PACKAGE_VERSION
    #define PACKAGE_VERSION "0.0"
#endif

#include "lobby.h"
#include "client.h"

bool debug = false;

//Default time after which connection is timed out.
static struct timeval timeout_interval = {LOBBY_SEND_INTERVAL + 5, 0};
//Just need one instance of this.
struct lobby_packet_info info_packet;

static void
readcb(struct bufferevent *bev, void *ctx)
{
    struct client * c = ctx;
    struct evbuffer *input = bufferevent_get_input(bev);
    enum lobby_packet_type packet_id;
    struct client * temp_client;
    //Incoming packets.
    struct lobby_packet_empty empty_packet;
    struct lobby_packet_register register_packet;
    struct lobby_packet_update update_packet;
    //Output packets.
    struct lobby_packet_list *list_packet;
    
    int i, n, item_count, failed;
    
    evbuffer_copyout(input, (void *) &packet_id,
                     sizeof(enum lobby_packet_type));
    
    switch (packet_id) {
        case PING:
            printf("[%s] Got PING packet!\n",
                inet_ntoa(c->address.sin_addr));
            n = bufferevent_read(bev, &empty_packet, sizeof(empty_packet));
            if (n != sizeof(empty_packet)) {
                printf("[%s] Got PING packet with wrong size.\n",
                       inet_ntoa(c->address.sin_addr));
                client_free_client(c);
            }
            break;
        case REGISTER:
            printf("[%s] Got REGISTER packet!\n",
                inet_ntoa(c->address.sin_addr));
            n = bufferevent_read(bev, &register_packet,
                                 sizeof(register_packet));
            if (n == sizeof(register_packet)) {
                if (!c->info) {
                    c->info = malloc(sizeof(struct server_info));
                    
                    c->info->server_port = register_packet.server_port;
                    memcpy(c->info->server_name, register_packet.server_name,
                           LOBBY_STR_LEN);
                    //Just in case.
                    c->info->server_name[LOBBY_STR_LEN - 1] = '\0';
                }
            } else {
                printf("[%s] Got REGISTER packet with wrong size.\n",
                       inet_ntoa(c->address.sin_addr));
                client_free_client(c);
            }
            break;
        case UPDATE:
            printf("[%s] Got UPDATE packet!\n",
                   inet_ntoa(c->address.sin_addr));
            n = bufferevent_read(bev, &update_packet, sizeof(update_packet));
            if (n == sizeof(update_packet)) {
                if (c->info) {
                    c->info->players_max = update_packet.players_max;
                    c->info->players_current = update_packet.players_current;
                    
                    memcpy(c->info->map_name, update_packet.map_name,
                           LOBBY_STR_LEN);
                    //Just in case.
                    c->info->map_name[LOBBY_STR_LEN - 1] = '\0';
                    
                    c->info->map_size_x = update_packet.map_size_x;
                    c->info->map_size_y = update_packet.map_size_y;
                } else {
                    printf("[%s] Got UPDATE packet before REGISTER packet.\n",
                           inet_ntoa(c->address.sin_addr));
                    client_free_client(c);
                }
            } else {
                printf("[%s] Got UPDATE packet with wrong size.\n",
                       inet_ntoa(c->address.sin_addr));
                client_free_client(c);
            }
            break;
        case GET_LIST:
            printf("[%s] Got GET_LIST packet!\n",
                   inet_ntoa(c->address.sin_addr));
            n = bufferevent_read(bev, &empty_packet, sizeof(empty_packet));
            if (n == sizeof(empty_packet)) {
                item_count = 0;
                temp_client = all_clients;
                while(temp_client) {
                    if (temp_client->info) {
                        item_count++;
                    }
                    temp_client = temp_client->prev;
                }
                list_packet = malloc(sizeof(struct lobby_packet_list) + 
                                     sizeof(struct lobby_list_item) *
                                     item_count);
                list_packet->packet_id = LIST;
                list_packet->item_count = item_count;
                
                i = 0;
                temp_client = all_clients;
                //Loads of copying (3x per item), could do better but meh.
                while(temp_client) {
                    if (temp_client->info) {
                        //Server info.
                        memcpy(list_packet->items[i].server_name,
                               temp_client->info->server_name, LOBBY_STR_LEN);
                        //Not very portable.
                        memcpy(&(list_packet->items[i].server_ip[0]),
                               &(temp_client->address.sin_addr.s_addr),
                               sizeof(uint32_t));
                        list_packet->items[i].server_port =
                               temp_client->info->server_port;
                        //Players info.
                        list_packet->items[i].players_max =
                               temp_client->info->players_max;
                        list_packet->items[i].players_current =
                               temp_client->info->players_current;
                        
                        //Map info.
                        memcpy(list_packet->items[i].map_name,
                               temp_client->info->map_name, LOBBY_STR_LEN);
                        list_packet->items[i].map_size_x =
                               temp_client->info->map_size_x;
                        list_packet->items[i].map_size_y =
                               temp_client->info->map_size_y;
                               
                        i++;
                    }
                    temp_client = temp_client->prev;
                }
                
                //Send it.
                failed = bufferevent_write(c->buf_event,
                                (const void *) list_packet,
                                sizeof(struct lobby_packet_list) + 
                                     sizeof(struct lobby_list_item) *
                                     list_packet->item_count);
                if(failed) {
                    printf("[%s] Failed to send LIST packet.\n",
                           inet_ntoa(c->address.sin_addr));    
                    client_free_client(c);          
                }
                 
                //Free the monster.
                free(list_packet);
            } else {
                printf("[%s] Got PING packet with wrong size.\n",
                       inet_ntoa(c->address.sin_addr));
                client_free_client(c);
            }
            break;
        default:
            printf("[%s] Got unknown packet!\n",
                   inet_ntoa(c->address.sin_addr));
            //Just drop the connection.
            client_free_client(c);
            break;
    }
}

static void
eventcb(struct bufferevent *bev, short events, void *ctx)
{
    struct client *c = ctx;
    struct lobby_packet_info info_packet;
    
    //Print error.
    if (events & BEV_EVENT_ERROR) {
        perror("Error from bufferevent");
    }
    //Client closed connection.
    if (events & BEV_EVENT_EOF) {
        printf("[%s] Client closed connection.\n",
               inet_ntoa(c->address.sin_addr));
    }
    //Client timed out.
    if (events & BEV_EVENT_TIMEOUT) {
        printf("[%s] Client timed out.\n",
               inet_ntoa(c->address.sin_addr));
    }
    
    //In case of error, end of connection or timeout, disconnect and free the
    //client.
    if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR | BEV_EVENT_TIMEOUT)) {
        client_free_client(c);
    }
}

static void
accept_conn_cb(struct evconnlistener *listener,
    evutil_socket_t fd, struct sockaddr *address, int socklen,
    void *ctx)
{
    struct sockaddr_in * addr = (struct sockaddr_in *) address;
    int failed = 0;
    //We got a connection.
    printf("[%s] New connection received.\n",
           inet_ntoa(addr->sin_addr));
    struct client *c = client_new_client();
    
    /* We got a new connection! Set up a bufferevent for it. */
    c->evloop = evconnlistener_get_base(listener);
    c->buf_event = bufferevent_socket_new(
            c->evloop, fd, BEV_OPT_CLOSE_ON_FREE);
    
    memcpy(&(c->address), address, sizeof(struct sockaddr));
    
    //Enable timeouts.
    bufferevent_set_timeouts(c->buf_event, &timeout_interval,
                             &timeout_interval);

    bufferevent_setcb(c->buf_event, readcb, NULL,
                    eventcb, (void *) c);

    bufferevent_enable(c->buf_event, EV_READ|EV_WRITE);
    
    //Send INFO packet.
    failed = bufferevent_write(c->buf_event, (const void *) &info_packet,
                        sizeof(info_packet));
    if(failed) {
        printf("[%s] Failed to send INFO packet.\n",
               inet_ntoa(c->address.sin_addr));              
    }
}

static void
accept_error_cb(struct evconnlistener *listener, void *ctx)
{
    struct event_base *base = evconnlistener_get_base(listener);
    int err = EVUTIL_SOCKET_ERROR();
    printf("Got an error %d (%s) on the listener. "
            "Shutting down.\n", err, evutil_socket_error_to_string(err));

    event_base_loopexit(base, NULL);
}

int
main(int argc, char **argv)
{
    char ch;
    int port, new_port = 0, timeout;
    bool daemon = false;
    char *conf_name = NULL;
    pid_t pid, sid;
    struct event_base *base;
    struct evconnlistener *listener;
    struct sockaddr_in sin;
    dictionary *conf_dic;
    char major[4], minor[4];
    int dot_pos;
    
    //Check the input flags.
    while ((ch = getopt(argc, argv, "dvpc:")) != -1) {
        switch (ch) {
            case 'd':
                daemon = true;
                break;
            case 'c':
                conf_name = optarg;
                break;
        }
    }
    
    if (daemon) {
        pid = fork();
        if (pid < 0) {
            exit(EXIT_FAILURE);
        } else if (pid > 0) {
            exit(EXIT_SUCCESS);
        }
        umask(0);
        sid = setsid();
        if (sid < 0) {
            exit(EXIT_FAILURE);
        }
    }
    
    //Parse PACKAGE_VERSION.
    dot_pos = strcspn(PACKAGE_VERSION, ".");
    if (dot_pos > 3) {
        err(1, "failed to parse PACKAGE_VERSION");
    }
    strncpy(major, PACKAGE_VERSION, dot_pos);
    major[dot_pos+1] = '\0';
    strcpy(minor, &PACKAGE_VERSION[dot_pos + 1]);
    info_packet.packet_id = INFO;
    info_packet.ver_major = atoi(major);
    info_packet.ver_minor = atoi(minor);
        
    //Load configuration file.
    if (conf_name) {
        conf_dic = iniparser_load(conf_name);
    } else {
        conf_dic = iniparser_load(CONF_FILE);
    }
    if (!conf_dic) {
        err(1, "failed top open config file");
    }

    port = iniparser_getint(conf_dic, "main:port", 0);
    if (!port) {
        err(1, "configuration file doesn't contain item 'port'");
    }
        
    timeout = iniparser_getint(conf_dic, "main:timeout", 0);
    if (timeout) {
        timeout_interval.tv_sec = timeout;
    }
    
    //Free configuration dictionary.
    iniparser_freedict(conf_dic);
    
    //Make default event base.
    base = event_base_new();
    if (!base) {
        err(1, "open base event failed");
    }
    
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(0);
    sin.sin_port = htons(port);

    //Try to bind to the socket.
    listener = evconnlistener_new_bind(base, accept_conn_cb, NULL,
        LEV_OPT_CLOSE_ON_FREE|LEV_OPT_REUSEABLE, -1,
        (struct sockaddr*)&sin, sizeof(sin));
    if (!listener) {
            err(1, "create listener failed");
    }
    
    //Set the error callback.
    evconnlistener_set_error_cb(listener, accept_error_cb);
    
    printf("Starting event loop.\n");
    
    //Start the event loop.	
    event_base_dispatch(base);
    
    //Free event base.
    event_base_free(base);
    
    //Free listener (also closes the socket).	
    free(listener);
    
    return (EXIT_SUCCESS);
}
