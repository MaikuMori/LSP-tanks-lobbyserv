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
//Logging.
#include <syslog.h>
//Files.
#include <fcntl.h>

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
    
    //Peak at packet_id.
    evbuffer_copyout(input, (void *) &packet_id,
                     sizeof(enum lobby_packet_type));
    
    switch (packet_id) {
        case LOBBY_PING:
            n = bufferevent_read(bev, &empty_packet, sizeof(empty_packet));
            if (n != sizeof(empty_packet)) {
                syslog(LOG_WARNING, "[%s] Got PING packet with "
                       "bad size (%d).", inet_ntoa(c->address.sin_addr), n);    
                client_free_client(c);
            }
            break;
        case LOBBY_REGISTER:
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
                syslog(LOG_WARNING, "[%s] Got REGISTER packet with "
                       "bad size (%d).", inet_ntoa(c->address.sin_addr), n);       
                client_free_client(c);
            }
            break;
        case LOBBY_UPDATE:
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
                    syslog(LOG_INFO, "[%s] Got UPDATE packet before "
                           "REGISTER packet.", inet_ntoa(c->address.sin_addr));
                    client_free_client(c);
                }
            } else {
                syslog(LOG_WARNING, "[%s] Got UPDATE packet with "
                       "bad size (%d).", inet_ntoa(c->address.sin_addr), n);
                client_free_client(c);
            }
            break;
        case LOBBY_GET_LIST:
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
                list_packet->packet_id = LOBBY_LIST;
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
                    syslog(LOG_WARNING, "[%s] Failed to send LIST packet.",
                           inet_ntoa(c->address.sin_addr));
                    client_free_client(c);          
                }
                 
                //Free the monster.
                free(list_packet);
            } else {
                syslog(LOG_WARNING, "[%s] Got GET_LIST packet with "
                       "bad size (%d).", inet_ntoa(c->address.sin_addr), n);
                client_free_client(c);
            }
            break;
        default:
            syslog(LOG_WARNING, "[%s] Got unknown packet (%d)!",
                   inet_ntoa(c->address.sin_addr), packet_id);
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
    int err;
    
    //Print error.
    if (events & BEV_EVENT_ERROR) {
        err = EVUTIL_SOCKET_ERROR();
        syslog(LOG_WARNING, "[%s] Bufferevent error %d: %s.",
               inet_ntoa(c->address.sin_addr),
               err, evutil_socket_error_to_string(err));
    }
    //Client closed connection.
    if (events & BEV_EVENT_EOF) {
        syslog(LOG_INFO, "[%s] Client closed connection.",
               inet_ntoa(c->address.sin_addr));
    }
    //Client timed out.
    if (events & BEV_EVENT_TIMEOUT) {
        syslog(LOG_INFO, "[%s] Client timed out.",
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
    syslog(LOG_INFO, "[%s] New connection received.",
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
    ///Set callbacks.
    bufferevent_setcb(c->buf_event, readcb, NULL,
                    eventcb, (void *) c);
    //Enable both reading and writing.
    bufferevent_enable(c->buf_event, EV_READ|EV_WRITE);
    
    //Send INFO packet.
    failed = bufferevent_write(c->buf_event, (const void *) &info_packet,
                        sizeof(info_packet));
    if(failed) {
        syslog(LOG_WARNING, "[%s] Failed to send INFO packet.",
               inet_ntoa(c->address.sin_addr));
    }
}

static void
accept_error_cb(struct evconnlistener *listener, void *ctx)
{
    struct event_base *base = evconnlistener_get_base(listener);
    int err = EVUTIL_SOCKET_ERROR();
    syslog(LOG_ALERT, "Got an error %d (%s) on the listener."
           "Shutting down.", err, evutil_socket_error_to_string(err));

    event_base_loopexit(base, NULL);
}

static void
sigterm_cb(evutil_socket_t fd, short events, void *ptr)
{
    struct event_base *base = (struct event_base *) ptr;
    syslog(LOG_WARNING, "Got SIGTERM. Shutting down!");
    client_free_all_clients();
    event_base_loopexit(base, NULL);
}

static void
daemonize()
{
    int i,lfp;
    char str[10];
    //Alreadt daemon?
	if(getppid() == 1) {
	    return;
	}
	//Fork.
	i=fork();
	if (i<0) exit(1);
	if (i>0) exit(0);

	//Get new process group.
	setsid();
	//Close all descriptors.
	for (i=getdtablesize(); i >= 0; --i) {
	     close(i);
     }
    //Standard I/O. 
	i=open("/dev/null", O_RDWR);
	dup(i);
	dup(i);
	//Set file permissions.
	umask(027);
	//Change CWD.
	chdir(RUNNING_DIR);
	//Open lock file.
	lfp = open(LOCK_FILE, O_RDWR | O_CREAT, 0640);
	if (lfp < 0) {
	    //Can't open lock file.
	    exit(1);
	}
	if (lockf(lfp, F_TLOCK, 0) < 0) {
	    //Can't lock file.
	    exit(0);
    } 
    //Get pid.
	sprintf(str, "%d\n", getpid());
	//Write it.
	write(lfp, str, strlen(str));
	//Signals.
	signal(SIGCHLD, SIG_IGN);
	signal(SIGTSTP, SIG_IGN);
	signal(SIGTTOU, SIG_IGN);
	signal(SIGTTIN, SIG_IGN);
	signal(SIGHUP, SIG_IGN);
	//Handle is from libevent.
	signal(SIGTERM, SIG_IGN);
}

int
main(int argc, char **argv)
{
    char ch;
    int port, timeout;
    bool daemon = false;
    char *conf_name = NULL;
    char major[4], minor[4];
    int dot_pos;
    dictionary *conf_dic;
    struct event_base *base;
    struct evconnlistener *listener;
    struct sockaddr_in sin;
    struct event *term_event;
    
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
        daemonize();
    }
    
    //Start logging.
    openlog("lobbyserv", LOG_PID, LOG_DAEMON);
    
    //Parse PACKAGE_VERSION.
    dot_pos = strcspn(PACKAGE_VERSION, ".");
    if (dot_pos > 3) {
        err(1, "failed to parse PACKAGE_VERSION");
        syslog(LOG_ALERT, "Failed to parse PACKAGE_VERSION.");
        return 1;
    }
    strncpy(major, PACKAGE_VERSION, dot_pos);
    major[dot_pos+1] = '\0';
    strcpy(minor, &PACKAGE_VERSION[dot_pos + 1]);
    info_packet.packet_id = LOBBY_INFO;
    info_packet.ver_major = atoi(major);
    info_packet.ver_minor = atoi(minor);
        
    //Load configuration file.
    if (conf_name) {
        conf_dic = iniparser_load(conf_name);
    } else {
        conf_dic = iniparser_load(CONF_FILE);
    }
    if (!conf_dic) {
        syslog(LOG_ALERT, "Can't open config file.");
        return 1;
    }

    port = iniparser_getint(conf_dic, "main:port", 0);
    if (!port) {
        syslog(LOG_ALERT, "Configuration file doesn't contain item 'port'.");
        return 1;
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
        syslog(LOG_ALERT, "Can't create event base.");
        return 1;
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
        syslog(LOG_ALERT, "Failed to create listening socket.");
        return 1;
    }
    
    //Set the error callback.
    evconnlistener_set_error_cb(listener, accept_error_cb);
    
    //Set up SIGTERM handler.
    term_event = evsignal_new(base, SIGTERM, sigterm_cb, (void *) base);
    event_add(term_event, NULL);
    
    syslog(LOG_INFO, "Starting main event loop.");
    
    //Start the event loop.	
    event_base_dispatch(base);
    
    syslog(LOG_INFO, "Shutting down.");
    
    //Free event base.
    event_base_free(base);
    
    //Free term_event event.
    event_free(term_event);
    
    //Free listener (also closes the socket).	
    free(listener);
    
    //Close log.
    closelog();
    
    return (EXIT_SUCCESS);
}
