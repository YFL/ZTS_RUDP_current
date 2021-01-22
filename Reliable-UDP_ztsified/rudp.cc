#include <iostream>

#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "event.h"
#include "rudp.h"
#include "rudp_api.h"

/** rudp.c
 *
 * This file implements the majority of the logic for RUDP sending and receiving
 *
 * Author: Andrew Keating
 */

#define DROP 0 /* Probability of packet loss */

typedef enum {SYN_SENT = 0, OPENING, OPEN, FIN_SENT} rudp_state_t; /* RUDP States */

struct rudp_packet
{
    rudp_hdr header;
    int payload_length;
    char payload[RUDP_MAXPKTSIZE];
};

/* Outgoing data queue */
struct data
{
    void *item;
    int len;
    data *next;
};

struct sender_session
{
    rudp_state_t status; /* Protocol state */
    uint32_t seqno;
    rudp_packet *sliding_window[RUDP_WINDOW];
    int retransmission_attempts[RUDP_WINDOW];
    data *data_queue; /* Queue of unsent data */
    bool session_finished; /* Has the FIN we sent been ACKed? */
    void *syn_timeout_arg; /* Argument pointer used to delete SYN timeout event */
    void *fin_timeout_arg; /* Argument pointer used to delete FIN timeout event */
    void *data_timeout_arg[RUDP_WINDOW]; /* Argument pointers used to delete DATA timeout events */
    int syn_retransmit_attempts;
    int fin_retransmit_attempts;
};

struct receiver_session
{
    rudp_state_t status; /* Protocol state */
    uint32_t expected_seqno;
    bool session_finished; /* Have we received a FIN from the sender? */
};

struct session
{
    sender_session *sender;
    receiver_session *receiver;
    zts_sockaddr_in6 address;
    session *next;
};

/* Keeps state for potentially multiple active sockets */
struct rudp_socket_list
{
    rudp_socket_t rsock;
    bool close_requested;
    int (*recv_handler)(rudp_socket_t, zts_sockaddr_in6 *, char *, int);
    int (*handler)(rudp_socket_t, rudp_event_t, zts_sockaddr_in6 *);
    session *sessions_list_head;
    rudp_socket_list *next;
};

/* Arguments for timeout callback function */
struct timeoutargs
{
    rudp_socket_t fd;
    rudp_packet *packet;
    zts_sockaddr_in6 *recipient;
};

/* Prototypes */
void create_sender_session(struct rudp_socket_list *socket, uint32_t seqno, struct zts_sockaddr_in6 *to, struct data **data_queue);
void create_receiver_session(struct rudp_socket_list *socket, uint32_t seqno, struct zts_sockaddr_in6 *addr);
rudp_packet *create_rudp_packet(uint16_t type, uint32_t seqno, int len, char *payload);
int compare_sockaddr(struct zts_sockaddr_in6 *s1, struct zts_sockaddr_in6 *s2);
int receive_callback(int file, void *arg);
int timeout_callback(int retry_attempts, void *args);
int send_packet(bool is_ack, rudp_socket_t rsocket, struct rudp_packet *p, struct zts_sockaddr_in6 *recipient);

/* Global variables */
bool rng_seeded = false;
rudp_socket_list *socket_list_head = NULL;

/* Creates a new sender session and appends it to the socket's session list */
void create_sender_session(struct rudp_socket_list *socket, uint32_t seqno, struct zts_sockaddr_in6 *to, struct data **data_queue)
{
    session *new_session = new (std::nothrow) session;
    if(new_session == NULL)
    {
        std::cerr << "create_sender_session: Error allocating memory" << std::endl;
        return;
    }
    new_session->address = *to;
    new_session->next = NULL;
    new_session->receiver = NULL;

    sender_session *new_sender_session = new (std::nothrow) sender_session;
    if(new_sender_session == NULL)
    {
        std::cerr << "create_sender_session: Error allocating memory" << std::endl;
        return;
    }
    new_sender_session->status = SYN_SENT;
    new_sender_session->seqno = seqno;
    new_sender_session->session_finished = false;
    /* Add data to the new session's queue */
    new_sender_session->data_queue = *data_queue;
    new_session->sender = new_sender_session;

    int i;
    for(i = 0; i < RUDP_WINDOW; i++)
    {
        new_sender_session->retransmission_attempts[i] = 0;
        new_sender_session->data_timeout_arg[i] = 0;
        new_sender_session->sliding_window[i] = NULL;
    }    
    new_sender_session->syn_retransmit_attempts = 0;
    new_sender_session->fin_retransmit_attempts = 0;
    
    if(socket->sessions_list_head == NULL)
    {
        socket->sessions_list_head = new_session;
    }
    else
    {
        session *curr_session = socket->sessions_list_head;
        while(curr_session->next != NULL)
        {
            curr_session = curr_session->next;
        }
        curr_session->next = new_session;
    }
}

/* Creates a new receiver session and appends it to the socket's session list */
void create_receiver_session(rudp_socket_list *socket, uint32_t seqno, zts_sockaddr_in6 *addr)
{
    session *new_session = new (std::nothrow) session;
    if(new_session == NULL)
    {
        std::cerr << "create_receiver_session: Error allocating memory" << std::endl;
        return;
    }
    new_session->address = *addr;
    new_session->next = NULL;
    new_session->sender = NULL;
    
    receiver_session *new_receiver_session = new (std::nothrow) receiver_session;
    if(new_receiver_session == NULL)
    {
        std::cerr << "create_receiver_session: Error allocating memory" << std::endl;
        return;
    }
    new_receiver_session->status = OPENING;
    new_receiver_session->session_finished = false;
    new_receiver_session->expected_seqno = seqno;
    new_session->receiver = new_receiver_session;
    
    if(socket->sessions_list_head == NULL)
    {
        socket->sessions_list_head = new_session;
    }
    else
    {
        session *curr_session = socket->sessions_list_head;
        while(curr_session->next != NULL)
        {
            curr_session = curr_session->next;
        }
        curr_session->next = new_session;
    }
}

/* Allocates a RUDP packet and returns a pointer to it */
rudp_packet *create_rudp_packet(uint16_t type, uint32_t seqno, int len, char *payload)
{
    rudp_hdr header;
    header.version = RUDP_VERSION;
    header.type = type;
    header.seqno = seqno;
    
    rudp_packet *packet = new (std::nothrow) rudp_packet;
    if(packet == NULL)
    {
        std::cerr << "create_rudp_packet: Error allocating memory for packet" << std::endl;
        return NULL;
    }
    packet->header = header;
    packet->payload_length = len;
    memset(&packet->payload, 0, RUDP_MAXPKTSIZE);
    if(payload != NULL)
    {
        memcpy(&packet->payload, payload, len);
    }
    
    return packet;
}

/* Returns 1 if the two sockaddr_in structs are equal and 0 if not */
int compare_sockaddr(zts_sockaddr_in6 *s1, zts_sockaddr_in6 *s2)
{
    char sender[ZTS_INET6_ADDRSTRLEN];
    char recipient[ZTS_INET6_ADDRSTRLEN];
    const char *err = zts_inet_ntop(ZTS_AF_INET6, &s1->sin6_addr, sender, ZTS_INET6_ADDRSTRLEN);
    if(!err)
    {
        return 0;
    }
    err = zts_inet_ntop(ZTS_AF_INET6, &s2->sin6_addr, recipient, ZTS_INET6_ADDRSTRLEN);
    
    return ((s1->sin6_family == s2->sin6_family) && (strcmp(sender, recipient) == 0) && (s1->sin6_port == s2->sin6_port));
}

/* Creates and returns a RUDP socket */
rudp_socket_t rudp_socket(int port)
{
    if(rng_seeded == false)
    {
        srand(time(NULL));
        rng_seeded = true;
    }
    int sockfd;
    zts_sockaddr_in6 address;

    sockfd = zts_socket(ZTS_AF_INET6, ZTS_SOCK_DGRAM, 0);
    if(sockfd < 0)
    {
        std::cerr << "Couldn't create socket: err: " << sockfd << " zts_errno: " << zts_errno << std::endl;
        return (rudp_socket_t) -1;
    }

    memset(&address, 0, sizeof(address));
    address.sin6_family = ZTS_AF_INET6;
    int err = zts_inet_pton(ZTS_AF_INET6, "::", &address.sin6_addr);
    if(err <= 0)
    {
        std::cerr << "Couldn't create zts_sockaddr_in6 from string address" << std::endl;
        return (rudp_socket_t) -1;
    }
    address.sin6_port = zts_htons(port);

    if((err = zts_bind(sockfd, (zts_sockaddr *)&address, sizeof(address))) < 0)
    {
        std::cerr << "Couldn't bind socket to address: err: " << err << " zts_errno: " << zts_errno << std::endl;
        return (rudp_socket_t) -1;
    }

    rudp_socket_t socket = (rudp_socket_t)sockfd;

    /* Create new socket and add to list of sockets */
    rudp_socket_list *new_socket = new (std::nothrow) rudp_socket_list;
    if(new_socket == NULL)
    {
        std::cerr << "rudp_socket: Error allocating memory for socket list" << std::endl;
        return (rudp_socket_t) -1;
    }
    new_socket->rsock = socket;
    new_socket->close_requested = false;
    new_socket->sessions_list_head = NULL;
    new_socket->next = NULL;
    new_socket->handler = NULL;
    new_socket->recv_handler = NULL;

    if(socket_list_head == NULL)
    {
        socket_list_head = new_socket;
    }
    else
    {
        rudp_socket_list *curr = socket_list_head;
        while(curr->next != NULL)
        {
            curr = curr->next;
        }
        curr->next = new_socket;
    }

    /* Register callback event for this socket descriptor */
    if(event_fd(sockfd, receive_callback, (void*) sockfd, "receive_callback") < 0)
    {
        std::cerr << "Error registering receive callback function" << std::endl;
    }

    return socket;
}

/* Callback function executed when something is received on fd */
int receive_callback(int file, void *arg)
{
    char buf[sizeof(rudp_packet)];
    struct zts_sockaddr_in6 sender;
    zts_socklen_t sender_length = sizeof(zts_sockaddr_in6);
    zts_recvfrom(file, &buf, sizeof(rudp_packet), 0, (zts_sockaddr *)&sender, &sender_length);

    struct rudp_packet *received_packet = new (std::nothrow) rudp_packet;
    if(received_packet == NULL)
    {
        std::cerr << "receive_callback: Error allocating packet" << std::endl;
        return -1;
    }
    memcpy(received_packet, &buf, sizeof(rudp_packet));
    
    rudp_hdr rudpheader = received_packet->header;
    char type[5];
    short t = rudpheader.type;
    if(t == 1)
        strcpy(type, "DATA");
    else if(t == 2)
        strcpy(type, "ACK");
    else if(t == 4)
        strcpy(type, "SYN");
    else if(t==5)
        strcpy(type, "FIN");
    else
        strcpy(type, "BAD");

    char sender_str[ZTS_INET6_ADDRSTRLEN];
    const char *err = zts_inet_ntop(ZTS_AF_INET6, &sender.sin6_addr, sender_str, ZTS_INET6_ADDRSTRLEN);
    printf("Received %s packet from %s:%d seq number=%u on socket=%d\n",type, sender_str, zts_ntohs(sender.sin6_port), rudpheader.seqno, file);

    /* Locate the correct socket in the socket list */
    if(socket_list_head == NULL)
    {
        std::cerr << "Error: attempt to receive on invalid socket. No sockets in the list" << std::endl;
        return -1;
    }
    else
    {
        /* We have sockets to check */
        rudp_socket_list *curr_socket = socket_list_head;
        while(curr_socket != NULL)
        {
            if(curr_socket->rsock == (rudp_socket_t)file)
            {
                break;
            }
            curr_socket = curr_socket->next;
        }
        if(curr_socket->rsock == (rudp_socket_t)file)
        {
            /* We found the correct socket, now see if a session already exists for this peer */
            if(curr_socket->sessions_list_head == NULL)
            {
                /* The list is empty, so we check if the sender has initiated the protocol properly (by sending a SYN) */
                if(rudpheader.type == RUDP_SYN)
                {
                    /* SYN Received. Create a new session at the head of the list */
                    uint32_t seqno = rudpheader.seqno + 1;
                    create_receiver_session(curr_socket, seqno, &sender);
                    /* Respond with an ACK */
                    rudp_packet *p = create_rudp_packet(RUDP_ACK, seqno, 0, NULL);
                    send_packet(true, (rudp_socket_t)file, p, &sender);
                    delete p;
                }
                else
                {
                    /* No sessions exist and we got a non-SYN, so ignore it */
                }
            }
            else
            {
                /* Some sessions exist to be checked */
                bool session_found = false;
                session *curr_session = curr_socket->sessions_list_head;
                session *last_session;
                while(curr_session != NULL)
                {
                    if(curr_session->next == NULL)
                    {
                        last_session = curr_session;
                    }
                    if(compare_sockaddr(&curr_session->address, &sender) == 1)
                    {
                        /* Found an existing session */
                        session_found = true;
                        break;
                    }

                    curr_session = curr_session->next;
                }
                if(session_found == false)
                {
                    /* No session was found for this peer */
                    if(rudpheader.type == RUDP_SYN)
                    {
                        /* SYN Received. Send an ACK and create a new session */
                        uint32_t seqno = rudpheader.seqno + 1;
                        create_receiver_session(curr_socket, seqno, &sender);          
                        rudp_packet *p = create_rudp_packet(RUDP_ACK, seqno, 0, NULL);
                        send_packet(true, (rudp_socket_t)file, p, &sender);
                        delete p;
                    }
                    else
                    {
                        /* Session does not exist and non-SYN received - ignore it */
                    }
                }
                else
                {
                /* We found a matching session */ 
                    if(rudpheader.type == RUDP_SYN)
                    {
                        if(curr_session->receiver == NULL || curr_session->receiver->status == OPENING)
                        {
                            /* Create a new receiver session and ACK the SYN*/
                            receiver_session *new_receiver_session = new (std::nothrow) receiver_session;
                            if(new_receiver_session == NULL)
                            {
                                std::cerr << "receive_callback: Error allocating receiver session" << std::endl;
                                return -1;
                            }
                            new_receiver_session->expected_seqno = rudpheader.seqno + 1;
                            new_receiver_session->status = OPENING;
                            new_receiver_session->session_finished = false;
                            curr_session->receiver = new_receiver_session;

                            int32_t seqno = curr_session->receiver->expected_seqno;
                            rudp_packet *p = create_rudp_packet(RUDP_ACK, seqno, 0, NULL);
                            send_packet(true, (rudp_socket_t)file, p, &sender);
                            delete p;
                        }
                        else
                        {
                            /* Received a SYN when there is already an active receiver session, so we ignore it */
                        }
                    }
                    if(rudpheader.type == RUDP_ACK)
                    {
                        uint32_t ack_sqn = received_packet->header.seqno;
                        if(curr_session->sender->status == SYN_SENT)
                        {
                            /* This an ACK for a SYN */
                            uint32_t syn_sqn = curr_session->sender->seqno;
                            if((ack_sqn - 1) == syn_sqn)
                            {
                                /* Delete the retransmission timeout */
                                event_timeout_delete(timeout_callback, curr_session->sender->syn_timeout_arg);
                                timeoutargs *t = (struct timeoutargs *)curr_session->sender->syn_timeout_arg;
                                delete t->packet;
                                delete t->recipient;
                                delete t;
                                curr_session->sender->status = OPEN;
                                while(curr_session->sender->data_queue != NULL)
                                {
                                    /* Check if the window is already full */
                                    if(curr_session->sender->sliding_window[RUDP_WINDOW-1] != NULL)
                                    {
                                        break;
                                    }
                                    else
                                    {
                                        int index;
                                        int i;
                                        /* Find the first unused window slot */
                                        for(i = RUDP_WINDOW-1; i >= 0; i--)
                                        {
                                            if(curr_session->sender->sliding_window[i] == NULL)
                                            {
                                                index = i;
                                            }
                                        }
                                        /* Send packet, add to window and remove from queue */
                                        u_int32_t seqno = ++syn_sqn;
                                        int len = curr_session->sender->data_queue->len;
                                        char *payload = (char *)curr_session->sender->data_queue->item;
                                        rudp_packet *datap = create_rudp_packet(RUDP_DATA, seqno, len, payload);
                                        curr_session->sender->seqno += 1;
                                        curr_session->sender->sliding_window[index] = datap;
                                        curr_session->sender->retransmission_attempts[index] = 0;
                                        data *temp = curr_session->sender->data_queue;
                                        curr_session->sender->data_queue = curr_session->sender->data_queue->next;
                                        delete[] temp->item;
                                        delete temp;

                                        send_packet(false, (rudp_socket_t)file, datap, &sender);
                                    }
                                }
                            }
                        }
                        else if(curr_session->sender->status == OPEN)
                        {
                            /* This is an ACK for DATA */
                            if(curr_session->sender->sliding_window[0] != NULL)
                            {
                                if(curr_session->sender->sliding_window[0]->header.seqno == (rudpheader.seqno-1))
                                {
                                    /* Correct ACK received. Remove the first window item and shift the rest left */
                                    event_timeout_delete(timeout_callback, curr_session->sender->data_timeout_arg[0]);
                                    timeoutargs *args = (struct timeoutargs *)curr_session->sender->data_timeout_arg[0];
                                    delete args->packet;
                                    delete args->recipient;
                                    delete args;
                                    delete curr_session->sender->sliding_window[0];

                                    int i;
                                    if(RUDP_WINDOW == 1)
                                    {
                                        curr_session->sender->sliding_window[0] = NULL;
                                        curr_session->sender->retransmission_attempts[0] = 0;
                                        curr_session->sender->data_timeout_arg[0] = NULL;
                                    }
                                    else
                                    {
                                        for(i = 0; i < RUDP_WINDOW - 1; i++)
                                        {
                                            curr_session->sender->sliding_window[i] = curr_session->sender->sliding_window[i+1];
                                            curr_session->sender->retransmission_attempts[i] = curr_session->sender->retransmission_attempts[i+1];
                                            curr_session->sender->data_timeout_arg[i] = curr_session->sender->data_timeout_arg[i+1];

                                            if(i == RUDP_WINDOW-2)
                                            {
                                                curr_session->sender->sliding_window[i+1] = NULL;
                                                curr_session->sender->retransmission_attempts[i+1] = 0;
                                                curr_session->sender->data_timeout_arg[i+1] = NULL;
                                            }
                                        }
                                    }

                                    while(curr_session->sender->data_queue != NULL)
                                    {
                                        if(curr_session->sender->sliding_window[RUDP_WINDOW-1] != NULL)
                                        {
                                            break;
                                        }
                                        else
                                        {
                                            int index;
                                            int i;
                                            /* Find the first unused window slot */
                                            for(i = RUDP_WINDOW-1; i >= 0; i--)
                                            {
                                                if(curr_session->sender->sliding_window[i] == NULL)
                                                {
                                                    index = i;
                                                }
                                            }                      
                                            /* Send packet, add to window and remove from queue */
                                            curr_session->sender->seqno = curr_session->sender->seqno + 1;                      
                                            uint32_t seqno = curr_session->sender->seqno;
                                            int len = curr_session->sender->data_queue->len;
                                            char *payload = (char *)curr_session->sender->data_queue->item;
                                            rudp_packet *datap = create_rudp_packet(RUDP_DATA, seqno, len, payload);
                                            curr_session->sender->sliding_window[index] = datap;
                                            curr_session->sender->retransmission_attempts[index] = 0;
                                            data *temp = curr_session->sender->data_queue;
                                            curr_session->sender->data_queue = curr_session->sender->data_queue->next;
                                            delete[] temp->item;
                                            delete temp;
                                            send_packet(false, (rudp_socket_t)file, datap, &sender);
                                        }
                                    }
                                    if(curr_socket->close_requested)
                                    {
                                        /* Can the socket be closed? */
                                        session *head_sessions = curr_socket->sessions_list_head;
                                        while(head_sessions != NULL)
                                        {
                                            if(head_sessions->sender->session_finished == false)
                                            {
                                                if(head_sessions->sender->data_queue == NULL &&  
                                                    head_sessions->sender->sliding_window[0] == NULL && 
                                                    head_sessions->sender->status == OPEN)
                                                {
                                                    head_sessions->sender->seqno += 1;                      
                                                    rudp_packet *p = create_rudp_packet(RUDP_FIN, head_sessions->sender->seqno, 0, NULL);
                                                    send_packet(false, (rudp_socket_t)file, p, &head_sessions->address);
                                                    delete p;
                                                    head_sessions->sender->status = FIN_SENT;
                                                }
                                            }
                                            head_sessions = head_sessions->next;
                                        }
                                    }
                                }
                            }
                        }
                        else if(curr_session->sender->status == FIN_SENT)
                        {
                            /* Handle ACK for FIN */
                            if((curr_session->sender->seqno + 1) == received_packet->header.seqno)
                            {
                                event_timeout_delete(timeout_callback, curr_session->sender->fin_timeout_arg);
                                timeoutargs *t = (timeoutargs *)curr_session->sender->fin_timeout_arg;
                                delete t->packet;
                                delete t->recipient;
                                delete t;
                                curr_session->sender->session_finished = true;
                                if(curr_socket->close_requested)
                                {
                                    /* See if we can close the socket */
                                    session *head_sessions = curr_socket->sessions_list_head;
                                    bool all_done = true;
                                    while(head_sessions != NULL)
                                    {
                                        if(head_sessions->sender->session_finished == false)
                                        {
                                            all_done = false;
                                        }
                                        else if(head_sessions->receiver != NULL && head_sessions->receiver->session_finished == false)
                                        {
                                            all_done = false;
                                        }
                                        else
                                        {
                                            delete head_sessions->sender;
                                            if(head_sessions->receiver)
                                            {
                                                delete head_sessions->receiver;
                                            }
                                        }

                                        session *temp = head_sessions;
                                        head_sessions = head_sessions->next;
                                        delete temp;
                                    }
                                    if(all_done)
                                    {
                                        if(curr_socket->handler != NULL)
                                        {
                                            curr_socket->handler((rudp_socket_t)file, RUDP_EVENT_CLOSED, &sender);
                                            event_fd_delete(receive_callback, (rudp_socket_t)file);
                                            zts_close(file);
                                            delete curr_socket;
                                        }
                                    }
                                }
                            }
                            else
                            {
                                /* Received incorrect ACK for FIN - ignore it */
                            }
                        }
                    }
                    else if(rudpheader.type == RUDP_DATA)
                    {
                        /* Handle DATA packet. If the receiver is OPENING, it can transition to OPEN */
                        if(curr_session->receiver->status == OPENING)
                        {
                            if(rudpheader.seqno == curr_session->receiver->expected_seqno)
                            {
                                curr_session->receiver->status = OPEN;
                            }
                        }

                        if(rudpheader.seqno == curr_session->receiver->expected_seqno)
                        {
                            /* Sequence numbers match - ACK the data */
                            uint32_t seqno = rudpheader.seqno + 1;
                            curr_session->receiver->expected_seqno = seqno;
                            rudp_packet *p = create_rudp_packet(RUDP_ACK, seqno, 0, NULL);
    
                            send_packet(true, (rudp_socket_t)file, p, &sender);
                            delete p;
                
                            /* Pass the data up to the application */
                            if(curr_socket->recv_handler != NULL)
                                curr_socket->recv_handler((rudp_socket_t)file, &sender, 
                                    (char*)&received_packet->payload, received_packet->payload_length);
                        }
                        /* Handle the case where an ACK was lost */
                        else if(SEQ_GEQ(rudpheader.seqno, (curr_session->receiver->expected_seqno - RUDP_WINDOW)) &&
                            SEQ_LT(rudpheader.seqno, curr_session->receiver->expected_seqno))
                        {
                            uint32_t seqno = rudpheader.seqno + 1;
                            rudp_packet *p = create_rudp_packet(RUDP_ACK, seqno, 0, NULL);
                            send_packet(true, (rudp_socket_t)file, p, &sender);
                            delete p;
                        }
                    }
                    else if(rudpheader.type == RUDP_FIN)
                    {
                        if(curr_session->receiver->status == OPEN)
                        {
                            if(rudpheader.seqno == curr_session->receiver->expected_seqno)
                            {
                                /* If the FIN is correct, we can ACK it */
                                uint32_t seqno = curr_session->receiver->expected_seqno + 1;
                                rudp_packet *p = create_rudp_packet(RUDP_ACK, seqno, 0, NULL);
                                send_packet(true, (rudp_socket_t)file, p, &sender);
                                delete p;
                                curr_session->receiver->session_finished = true;

                                if(curr_socket->close_requested)
                                {
                                    /* Can we close the socket now? */
                                    session *head_sessions = curr_socket->sessions_list_head;
                                    int all_done = true;
                                    while(head_sessions != NULL)
                                    {
                                        if(head_sessions->sender->session_finished == false)
                                        {
                                            all_done = false;
                                        }
                                        else if(head_sessions->receiver != NULL && head_sessions->receiver->session_finished == false)
                                        {
                                            all_done = false;
                                        }
                                        else
                                        {
                                            delete head_sessions->sender;
                                            if(head_sessions->receiver)
                                            {
                                                delete head_sessions->receiver;
                                            }
                                        }
                        
                                        session *temp = head_sessions;
                                        head_sessions = head_sessions->next;
                                        delete temp;
                                    }
                                    if(all_done)
                                    {
                                        if(curr_socket->handler != NULL)
                                        {
                                            curr_socket->handler((rudp_socket_t)file, RUDP_EVENT_CLOSED, &sender);
                                            event_fd_delete(receive_callback, (rudp_socket_t)file);
                                            zts_close(file);
                                            delete curr_socket;
                                        }
                                    }
                                }
                            }
                            else
                            {
                                /* FIN received with incorrect sequence number - ignore it */
                            }
                        }
                    }
                }
            }
        }
    }

    delete received_packet;
    return 0;
}

/* Close a RUDP socket */
int rudp_close(rudp_socket_t rsocket)
{
    rudp_socket_list *curr_socket = socket_list_head;
    while(curr_socket->next != NULL)
    {
        if(curr_socket->rsock == rsocket)
        {
            break;
        }
        curr_socket = curr_socket->next;
    }
    if(curr_socket->rsock == rsocket)
    {
        curr_socket->close_requested = true;        
        return 0;
    }
    
    return -1;
}

/* Register receive callback function */ 
int rudp_recvfrom_handler(
    rudp_socket_t rsocket,
    int (*handler)(
        rudp_socket_t, 
        zts_sockaddr_in6 *,
        char *,
        int
    )
)
{
    if(handler == NULL)
    {
        std::cerr << "rudp_recvfrom_handler failed: handler callback is null" << std::endl;
        return -1;
    }
    /* Find the proper socket from the socket list */
    rudp_socket_list *curr_socket = socket_list_head;
    while(curr_socket->next != NULL)
    {
        if(curr_socket->rsock == rsocket)
        {
            break;
        }
        curr_socket = curr_socket->next;
    }
    /* Extra check to handle the case where an invalid rsock is used */
    if(curr_socket->rsock == rsocket)
    {
        curr_socket->recv_handler = handler;
        return 0;
    }
    return -1;
}

/* Register event handler callback function with a RUDP socket */
int rudp_event_handler(
    rudp_socket_t rsocket, 
    int (*handler)(
        rudp_socket_t,
        rudp_event_t,
        zts_sockaddr_in6 *
    )
)
{
    if(handler == NULL)
    {
        std::cerr << "rudp_event_handler failed: handler callback is null" << std::endl;
        return -1;
    }

    /* Find the proper socket from the socket list */
    rudp_socket_list *curr_socket = socket_list_head;
    while(curr_socket->next != NULL)
    {
        if(curr_socket->rsock == rsocket)
        {
            break;
        }
        curr_socket = curr_socket->next;
    }

    /* Extra check to handle the case where an invalid rsock is used */
    if(curr_socket->rsock == rsocket)
    {
        curr_socket->handler = handler;
        return 0;
    }
    return -1;
}


/* Sends a block of data to the receiver. Returns 0 on success, -1 on error */
int rudp_sendto(rudp_socket_t rsocket, void* data, int len, zts_sockaddr_in6 *to)
{
    if(len < 0 || len > RUDP_MAXPKTSIZE)
    {
        std::cerr << "rudp_sendto Error: attempting to send with invalid max packet size" << std::endl;
        return -1;
    }

    if(rsocket == (rudp_socket_t)-1)
    {
        std::cerr << "rudp_sendto Error: attempting to send on invalid socket" << std::endl;
        return -1;
    }

    if(to == NULL)
    {
        std::cerr << "rudp_sendto Error: attempting to send to an invalid address" << std::endl;
        return -1;
    }

    bool new_session_created = true;
    uint32_t seqno = 0;
    if(socket_list_head == NULL)
    {
        std::cerr << "Error: attempt to send on invalid socket. No sockets in the list" << std::endl;
        return -1;
    }
    else
    {
        /* Find the correct socket in our list */
        rudp_socket_list *curr_socket = socket_list_head;
        while(curr_socket != NULL)
        {
            if(curr_socket->rsock == rsocket)
            {
                break;
            }
            curr_socket = curr_socket->next;
        }
        if(curr_socket->rsock == rsocket)
        {
            /* We found the correct socket, now see if a session already exists for this peer */
            struct data *data_item = new (std::nothrow) struct data;
            if(data_item == NULL)
            {
                std::cerr << "rudp_sendto: Error allocating data queue" << std::endl;
                return -1;
            }  
            data_item->item = new (std::nothrow) char[len];
            if(data_item->item == NULL)
            {
                std::cerr << "rudp_sendto: Error allocating data queue item" << std::endl;
                return -1;
            }
            memcpy(data_item->item, data, len);
            data_item->len = len;
            data_item->next = NULL;

            if(curr_socket->sessions_list_head == NULL)
            {
                /* The list is empty, so we create a new sender session at the head of the list */
                seqno = rand();
                create_sender_session(curr_socket, seqno, to, &data_item);
            }
            else
            {
                bool session_found = false;
                session *curr_session = curr_socket->sessions_list_head;
                session *last_in_list;
                while(curr_session != NULL)
                {
                    if(compare_sockaddr(&curr_session->address, to) == 1)
                    {
                        bool data_is_queued = false;
                        bool we_must_queue = true;

                        if(curr_session->sender==NULL)
                        {
                            seqno = rand();
                            create_sender_session(curr_socket, seqno, to, &data_item);
                            rudp_packet *p = create_rudp_packet(RUDP_SYN, seqno, 0, NULL);            
                            send_packet(false, rsocket, p, to);
                            delete p;
                            new_session_created = false ; /* Dont send the SYN twice */
                            break;
                        }

                        if(curr_session->sender->data_queue != NULL)
                            data_is_queued = true;

                        if(curr_session->sender->status == OPEN && !data_is_queued)
                        {
                            int i;
                            for(i = 0; i < RUDP_WINDOW; i++)
                            {
                                if(curr_session->sender->sliding_window[i] == NULL)
                                {
                                    curr_session->sender->seqno = curr_session->sender->seqno + 1;
                                    rudp_packet *datap = create_rudp_packet(RUDP_DATA, curr_session->sender->seqno, len, (char *)data);
                                    curr_session->sender->sliding_window[i] = datap;
                                    curr_session->sender->retransmission_attempts[i] = 0;
                                    send_packet(false, rsocket, datap, to);
                                    we_must_queue = false;
                                    break;
                                }
                            }
                        }

                        if(we_must_queue == true)
                        {
                            if(curr_session->sender->data_queue == NULL)
                            {
                                /* First entry in the data queue */
                                curr_session->sender->data_queue = data_item;
                            }
                            else
                            {
                                /* Add to end of data queue */
                                struct data *curr_socket = curr_session->sender->data_queue;
                                while(curr_socket->next != NULL)
                                {
                                    curr_socket = curr_socket->next;
                                }
                                curr_socket->next = data_item;
                            }
                        }

                        session_found = true;
                        new_session_created = false;
                        break;
                    }
                    if(curr_session->next==NULL)
                        last_in_list=curr_session;

                    curr_session = curr_session->next;
                }
                if(!session_found)
                {
                    /* If not, create a new session */
                    seqno = rand();
                    create_sender_session(curr_socket, seqno, to, &data_item);
                }
            }
        }
        else
        {
            std::cerr << "Error: attempt to send on invalid socket. Socket not found" << std::endl;
            return -1;
        }
    }
    if(new_session_created == true)
    {
        /* Send the SYN for the new session */
        rudp_packet *p = create_rudp_packet(RUDP_SYN, seqno, 0, NULL);    
        send_packet(false, rsocket, p, to);
        delete p;
    }
    return 0;
}

/* Callback function when a timeout occurs */
int timeout_callback(int fd, void *args)
{
    timeoutargs *timeargs=(timeoutargs *)args;
    rudp_socket_list *curr_socket = socket_list_head;
    while(curr_socket != NULL)
    {
        if(curr_socket->rsock == timeargs->fd)
        {
            break;
        }
        curr_socket = curr_socket->next;
    }
    if(curr_socket->rsock == timeargs->fd)
    {
        bool session_found = false;
        /* Check if we already have a session for this peer */
        session *curr_session = curr_socket->sessions_list_head;
        while(curr_session != NULL)
        {
            if(compare_sockaddr(&curr_session->address, timeargs->recipient) == 1)
            {
                /* Found an existing session */
                session_found = true;
                break;
            }
            curr_session = curr_session->next;
        }
        if(session_found == true)
        {
            if(timeargs->packet->header.type == RUDP_SYN)
            {
                if(curr_session->sender->syn_retransmit_attempts >= RUDP_MAXRETRANS)
                {
                    curr_socket->handler(timeargs->fd, RUDP_EVENT_TIMEOUT, timeargs->recipient);
                }
                else
                {
                    curr_session->sender->syn_retransmit_attempts++;
                    send_packet(false, timeargs->fd, timeargs->packet, timeargs->recipient);
                    delete timeargs->packet;
                    timeargs->packet = nullptr;
                }
            }
            else if(timeargs->packet->header.type == RUDP_FIN)
            {
                if(curr_session->sender->fin_retransmit_attempts >= RUDP_MAXRETRANS)
                {
                    curr_socket->handler(timeargs->fd, RUDP_EVENT_TIMEOUT, timeargs->recipient);
                }
                else
                {
                    curr_session->sender->fin_retransmit_attempts++;
                    send_packet(false, timeargs->fd, timeargs->packet, timeargs->recipient);
                    delete timeargs->packet;
                    timeargs->packet = nullptr;
                }
            }
            else
            {
                int i;
                int index;
                for(i = 0; i < RUDP_WINDOW; i++)
                {
                    if(curr_session->sender->sliding_window[i] != NULL && 
                        curr_session->sender->sliding_window[i]->header.seqno == timeargs->packet->header.seqno)
                    {
                        index = i;
                    }
                }

                if(curr_session->sender->retransmission_attempts[index] >= RUDP_MAXRETRANS)
                {
                    curr_socket->handler(timeargs->fd, RUDP_EVENT_TIMEOUT, timeargs->recipient);
                }
                else
                {
                    curr_session->sender->retransmission_attempts[index]++;
                    send_packet(false, timeargs->fd, timeargs->packet, timeargs->recipient);
                    delete timeargs->packet;
                    timeargs->packet = nullptr;
                }
            }
        }
    }

    delete timeargs->packet;
    delete timeargs->recipient;
    delete timeargs;
    return 0;
}

/* Transmit a packet via UDP */
int send_packet(bool is_ack, rudp_socket_t rsocket, rudp_packet *p, zts_sockaddr_in6 *recipient)
{
    char type[5];
    short t=p->header.type;
    if(t == 1)
        strcpy(type, "DATA");
    else if(t == 2)
        strcpy(type, "ACK");
    else if(t == 4)
        strcpy(type, "SYN");
    else if(t == 5)
        strcpy(type, "FIN");
    else
        strcpy(type, "BAD");

    char recipient_str[ZTS_INET6_ADDRSTRLEN];
    const char *err = zts_inet_ntop(ZTS_AF_INET6, &recipient->sin6_addr, recipient_str, ZTS_INET6_ADDRSTRLEN);
    if(!err)
    {
        std::cerr << "Couldn't convert zts_sockaddr_in6 to string" << std::endl;
        return -1;
    }
    std::cout << "Sending " << type << "packet to " << recipient_str << ':' << zts_ntohs(recipient->sin6_port)
        << " seq number=" << p->header.seqno << " on socket=" << rsocket << std::endl;

    if (DROP != 0 && rand() % DROP == 1)
    {
        std::cout << "Dropped" << std::endl;
    }
    else
    {
        if (zts_sendto(static_cast<int>((uint64_t)rsocket), p, sizeof(rudp_packet), 0, (zts_sockaddr *)recipient, sizeof(zts_sockaddr_in6)) < 0)
        {
            std::cerr << "rudp_sendto: sendto failed" << std::endl;
            return -1;
        }
    }

    if(!is_ack)
    {
        /* Set a timeout event if the packet isn't an ACK */
        timeoutargs *timeargs = new (std::nothrow) timeoutargs;
        if(timeargs == NULL)
        {
            std::cerr << "send_packet: Error allocating timeout args" << std::endl;
            return -1;
        }
        timeargs->packet = new (std::nothrow) rudp_packet;
        if(timeargs->packet == NULL)
        {
            std::cerr << "send_packet: Error allocating timeout args packet" << std::endl;
            return -1;
        }
        timeargs->recipient = new (std::nothrow) zts_sockaddr_in6;
        if(timeargs->packet == NULL)
        {
            std::cerr << "send_packet: Error allocating timeout args recipient" << std::endl;
            return -1;
        }
        timeargs->fd = rsocket;
        memcpy(timeargs->packet, p, sizeof(rudp_packet));
        memcpy(timeargs->recipient, recipient, sizeof(zts_sockaddr_in6));  
    
        zts_timeval currentTime;
        gettimeofday((timeval *)&currentTime, NULL);
        zts_timeval delay;
        delay.tv_sec = RUDP_TIMEOUT/1000;
        delay.tv_usec= 0;
        zts_timeval timeout_time;
        timeradd(&currentTime, &delay, &timeout_time);

        rudp_socket_list *curr_socket = socket_list_head;
        while(curr_socket != NULL)
        {
            if(curr_socket->rsock == timeargs->fd)
            {
                break;
            }
            curr_socket = curr_socket->next;
        }
        if(curr_socket->rsock == timeargs->fd)
        {
            bool session_found = false;
            /* Check if we already have a session for this peer */
            session *curr_session = curr_socket->sessions_list_head;
            while(curr_session != NULL)
            {
                if(compare_sockaddr(&curr_session->address, timeargs->recipient) == 1)
                {
                    /* Found an existing session */
                    session_found = true;
                    break;
                }
                curr_session = curr_session->next;
            }
            if(session_found)
            {
                if(timeargs->packet->header.type == RUDP_SYN)
                {
                    curr_session->sender->syn_timeout_arg = timeargs;
                }
                else if(timeargs->packet->header.type == RUDP_FIN)
                {
                    curr_session->sender->fin_timeout_arg = timeargs;
                }
                else if(timeargs->packet->header.type == RUDP_DATA)
                {
                    int i;
                    int index;
                    for(i = 0; i < RUDP_WINDOW; i++)
                    {
                        if(curr_session->sender->sliding_window[i] != NULL && 
                            curr_session->sender->sliding_window[i]->header.seqno == timeargs->packet->header.seqno)
                        {
                            index = i;
                        }
                    }
                    curr_session->sender->data_timeout_arg[index] = timeargs;
                }
            }
        }
        event_timeout(timeout_time, timeout_callback, timeargs, "timeout_callback");
    }
    return 0;
}
