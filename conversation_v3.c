#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <net/if.h>

#define MAX_LINE 1024
#define MAX_WINDOW_SIZE 10
#define TIMER_INTERVAL 300 // milliseconds

// Structure for packets
typedef struct {
    int seq_num;
    char data[MAX_LINE];
} Packet;

// Timer structure for managing retransmissions NEW
typedef struct Timer {
    int seq_num;
    int timeout; // in ms
    struct Timer *next;
} Timer;

// Timer management functions NEW
void add_timer(Timer **head, int seq_num, int timeout);
void decrement_timers(Timer **head, int interval);
void remove_timer(Timer **head, int seq_num);

// Function prototypes
void sender(const char *filename, const char *multicast_addr, int port, int window_size);
void sender_hello(int sock, struct sockaddr_in6 *mc_addr);
void receiver(const char *filename, const char *multicast_addr, int port);

/**
 * Main function: Handles arguments and determines sender/receiver role.
 */
int main(int argc, char *argv[]) {
    if (argc < 5) {
        fprintf(stderr, "Usage: %s <role> <filename> <multicast_addr> <port> [window_size]\n", argv[0]);
        fprintf(stderr, "Role: sender | receiver\n");
        return 1;
    }

    const char *role = argv[1];
    const char *filename = argv[2];
    const char *multicast_addr = argv[3];
    int port = atoi(argv[4]);
    int window_size = (argc == 6) ? atoi(argv[5]) : 1;

    if (strcmp(role, "sender") == 0) {
        sender(filename, multicast_addr, port, window_size);
    } else if (strcmp(role, "receiver") == 0) {
        receiver(filename, multicast_addr, port);
    } else {
        fprintf(stderr, "Invalid role: Use sender or receiver.\n");
        return 1;
    }

    return 0;
}

/**
 * Sender function: Reads a file and sends its content line by line via UDP multicast.
 */
void sender(const char *filename, const char *multicast_addr, int port, int window_size) {

    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("Error opening file");
        exit(1);
    }

    printf("File : '%s' is ready to send...\n", filename);

    int sock = socket(AF_INET6, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        fclose(file);
        exit(1);
    }

    //printf("Socket: %d\n",sock);

    // Enable address reuse for multicast NEW
    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));


    //printf("Socket: %d\n",sock);

    struct sockaddr_in6 addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(port);
    
    char multicast_addr_with_scope[64];
    snprintf(multicast_addr_with_scope, sizeof(multicast_addr_with_scope), "%s%%%s", multicast_addr, "bond0");
    inet_pton(AF_INET6, multicast_addr_with_scope, &addr.sin6_addr);

    //inet_pton(AF_INET6, multicast_addr, &addr.sin6_addr);

    /*
    struct ipv6_mreq mreq;
    inet_pton(AF_INET6, multicast_addr, &mreq.ipv6mr_multiaddr);
    mreq.ipv6mr_interface = 0; // Verwenden Sie das Standard-Interface
    if (setsockopt(sock, IPPROTO_IPV6, IPV6_JOIN_GROUP, &mreq, sizeof(mreq)) < 0) {
        perror("setsockopt(IPV6_JOIN_GROUP) failed");
        exit(1);
    }
    */

    // CONNECT phase: Send HELLO and wait for ACK_HELLO NEW
    sender_hello(sock, &addr);
    


    char line[MAX_LINE];
    int seq_num = 0;
    Packet window[MAX_WINDOW_SIZE];
    int window_start = 0;

    //NEW
    Timer *timer_list = NULL;
    

    while (fgets(line, sizeof(line), file)) {
        // Create packet
        Packet pkt;
        //pkt.seq_num = seq_num++;
        
        pkt.seq_num = htons(seq_num); // Konvertieren in Netzwerk-Byte-Reihenfolge
        //strncpy(pkt.data, line, MAX_LINE);
        strncpy(pkt.data, line, MAX_LINE - 1);
        pkt.data[MAX_LINE - 1] = '\0'; // Sicherstellen, dass der String null-terminiert ist

        //printf("Paket: %s⁄n", pkt.data);

        // Add to send window
        window[window_start % window_size] = pkt;
        window_start++;

        // Send packet
        //sendto(sock, &pkt, sizeof(pkt), 0, (struct sockaddr *)&addr, sizeof(addr));
        printf("Sender: SeqNum=%d, Data=%s\n", ntohs(pkt.seq_num), pkt.data);

        //printf("%s\n",pkt.data);

        int testSEND = sendto(sock, &pkt, sizeof(int) + strlen(pkt.data) , 0, (struct sockaddr *)&addr, sizeof(addr));
        printf("%d\n",testSEND);
        //printf("Sent: SeqNum=%d\n", ntohs(pkt.seq_num));
        seq_num++;

        // Add timer for packet NEW
        add_timer(&timer_list, ntohs(pkt.seq_num), TIMER_INTERVAL * 3);

        //DEBUGGING:
        //printf("Sending to %s:%d\n", multicast_addr, port);
        //Not sure???¿¿¿¿
        
        // Simulate timer and wait for NACK
        fd_set readfds;
        struct timeval timeout;
        timeout.tv_sec = 1;
       // timeout.tv_usec = TIMER_INTERVAL * 1000;
        timeout.tv_usec = 0;

        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);

        //printf("%d\n",select(sock, &readfds, NULL, NULL, &timeout));

        if (select(sock + 1, &readfds, NULL, NULL, &timeout) > 0) {
            // Receive NACK
            int nack_seq;
            struct sockaddr_in6 sender_addr;
            //printf("dsdsdsdsdsdsds\n");
            socklen_t sender_len = sizeof(sender_addr);
            recvfrom(sock, &nack_seq, sizeof(nack_seq), 0, (struct sockaddr *)&sender_addr, &sender_len);

            //recvfrom(sock, &nack_seq, sizeof(nack_seq), 0, NULL, NULL);
            printf("Received NACK: SeqNum=%d\n", nack_seq);

            printf("WINDOW SIZE: %d\n",window_size);

            // Resend packet
            for (int i = 0; i < window_size; i++) {
                printf("NACK: %d\n",nack_seq);
                printf("???: %d\n",window[i % window_size].seq_num);
                if (window[i % window_size].seq_num == nack_seq) {
                    
                   // if (pkt.seq_num == ntohs(timer_list->seq_num)) {
                    remove_timer(&timer_list, nack_seq);
                    printf("Timer removed for seq_num=%d\n", nack_seq);
                    //}

                    //remove_timer(&timer_list,nack_seq);
                    //printf("Timer removed for seq_num=%d\n", nack_seq);


                    sendto(sock, &window[i % window_size], sizeof(Packet), 0, (struct sockaddr *)&addr, sizeof(addr));
                    printf("Resent: SeqNum=%d\n", nack_seq);
                    //NEW
                    add_timer(&timer_list, nack_seq, TIMER_INTERVAL * 3);
                    break;
                }else decrement_timers(&timer_list, TIMER_INTERVAL); 
            }
        }

        //NEW
        
        
    }

    fclose(file);
    close(sock);
}

/**
 * HELLO phase: Sender sends HELLO to discover active receivers. NEW
 */
void sender_hello(int sock, struct sockaddr_in6 *mc_addr) {
    char hello_msg[] = "HELLO";
    int sended = sendto(sock, hello_msg, strlen(hello_msg), 0, (struct sockaddr *)mc_addr, sizeof(*mc_addr));
    printf("HELLO sent to multicast group : %d\n",sended);

    fd_set readfds;
    struct timeval timeout = {0, 300 * 1000}; // 300ms Timeout
    FD_ZERO(&readfds);
    FD_SET(sock, &readfds);

    int active_receivers = 0;

    // WIRD NICHT BEACHTET?
    while (select(sock + 1, &readfds, NULL, NULL, &timeout) > 0) {
        char buffer[16];  //char ack_msg[16];
        printf("Uhu,select worked!\n");

        struct sockaddr_in6 sender_addr;
        socklen_t sender_len = sizeof(sender_addr);


        //recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr *)&sender_addr, &sender_len);

        // Empfangen von ACK_HELLO
        int bytes_received = recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr *)&sender_addr, &sender_len);
        buffer[15] = '\0'; // Null-terminieren
        if (bytes_received > 0) {
            printf("Received message: %s\n", buffer);
            if (strcmp(buffer, "ACK_HELLO") == 0) {
                printf("ACK_HELLO received from a receiver.\n");
                active_receivers++;
            }
        }
        break;
        //printf("%s\n",buffer);

        //socklen_t sender_len = sizeof(mc_addr);
        //recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr *)&mc_addr, &sender_len);


        //recvfrom(sock, ack_msg, sizeof(ack_msg), 0, NULL, NULL); // HELLO

        /*
            if (strcmp(buffer, "HELLO") == 0) { //ACK_HELLO
            printf("Received ACK_HELLO from a receiver.\n");
            char ack_msg[] = "ACK_HELLO";
            sendto(sock, ack_msg, strlen(ack_msg), 0, (struct sockaddr *)&sender_addr, sender_len);
            active_receivers++;
        }
        */
        

    }

    if (active_receivers == 0) {
        printf("No active receivers. Aborting.\n");
        exit(1);
    }
    printf("Active receivers: %d\n", active_receivers);
    


    /*
        char hello_msg[] = "HELLO";
    int sended = sendto(sock, hello_msg, strlen(hello_msg), 0, (struct sockaddr *)mc_addr, sizeof(*mc_addr)); // Sending
    printf("HELLO sent to multicast group.: %d\n",sended);

    fd_set readfds;
    struct timeval timeout = {1,0}; // 300ms Timeout
    FD_ZERO(&readfds);
    FD_SET(sock, &readfds);

    int active_receivers = 0;

    // Set a deadline to limit the wait time for ACKs
    struct timeval start_time, end_time;
    gettimeofday(&start_time, NULL);


    //printf("ICH BIN HIER: sock:%d\n",sock);
    //printf("select value: %d\n",select(sock + 1 , &readfds, NULL, NULL, &timeout));

    //recvfrom(sock, "ack_msg", sizeof("ack_msg"), SO_RCVBUF , NULL, NULL);
    while (select(sock + 1 , &readfds, NULL, NULL, &timeout) > 0) {
        char ack_msg[16];
        recvfrom(sock, ack_msg, sizeof(ack_msg), 0, NULL, NULL);
        if (strcmp(ack_msg, "ACK_HELLO") == 0) {
            active_receivers++;
            printf("Received ACK_HELLO from a receiver.\n");
        }

        // Check elapsed time since sending HELLO message
        gettimeofday(&end_time, NULL);
        long elapsed_ms = (end_time.tv_sec - start_time.tv_sec) * 1000 + (end_time.tv_usec - start_time.tv_usec) / 1000;
        if (elapsed_ms > 300) { // 300ms Timeout reached
            break;
        }
    }

    if (active_receivers == 0) {
        printf("No active receivers. Aborting.\n");
        exit(1);
    }

    printf("Active receivers: %d\n", active_receivers);
    */
    


    
}

/**
 * Receiver function: Listens for multicast packets and writes them to a file.
 */
void receiver(const char *filename, const char *multicast_addr, int port) {

    FILE *file = fopen(filename, "a");
    if (!file) {
        perror("Error opening file");
        exit(1);
    }

    printf("File : '%s' is ready to receiv...\n", filename);

    int sock = socket(AF_INET6, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        fclose(file);
        exit(1);
    }

    // Enable address reuse for multicast NEW
    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in6 addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(port);
    //inet_pton(AF_INET6, multicast_addr, &addr.sin6_addr);
    addr.sin6_addr = in6addr_any;

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("Bind failed");
        fclose(file);
        close(sock);
        exit(1);
    }

    struct ipv6_mreq mreq;
    inet_pton(AF_INET6, multicast_addr, &mreq.ipv6mr_multiaddr);
    //mreq.ipv6mr_interface = 0; // Verwenden Sie das Standard-Interface
    mreq.ipv6mr_interface = if_nametoindex("bond0");
    if (setsockopt(sock, IPPROTO_IPV6, IPV6_JOIN_GROUP, &mreq, sizeof(mreq)) < 0) {
        perror("setsockopt(IPV6_JOIN_GROUP) failed");
        //NEW
        close(sock);
        exit(1);
    }

    /*
        if (inet_pton(AF_INET6, multicast_addr, &mreq.ipv6mr_multiaddr) != 1) {
        perror("Invalid multicast address");
        exit(1);
    }
    */   
   
    Packet pkt;
    int expected_seq = 0;

    while (1) {
        /*
            printf("Waiting for packets...\n");
        int bytes_received = recvfrom(sock, &pkt, sizeof(pkt), 0, NULL, NULL);
        if (bytes_received > 0) {
             printf("Received: SeqNum=%d, Data=%s\n", pkt.seq_num, pkt.data);
        } else {
            perror("recvfrom failed");
        } 
        */
                  

        //recvfrom(sock, &pkt, sizeof(pkt), 0, NULL, NULL);
       
        //fopen(filename,"a");


        /*
        char buffer[16];
        struct sockaddr_in6 sender_addr;
        socklen_t sender_len = sizeof(sender_addr);


        recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr *)&sender_addr, &sender_len);
        buffer[15] = '\0'; // Null-terminieren für sicheren Vergleich
        printf("Received: %s\n", buffer);
        
        

        if (strcmp(buffer, "HELLO") == 0) {
            printf("Received HELLO. Sending ACK_HELLO.\n");
            char ack_msg[] = "ACK_HELLO";
            sendto(sock, ack_msg, strlen(ack_msg), 0, (struct sockaddr *)&sender_addr, sender_len);
        }
        

        if (pkt.seq_num != expected_seq) {
            printf("Sending NACK: SeqNum=%d\n", expected_seq);
            int nack_seq = htons(expected_seq);
            //sendto(sock, &expected_seq, sizeof(expected_seq), 0, (struct sockaddr *)&sender_addr, sender_len);
            sendto(sock, &nack_seq, sizeof(expected_seq), 0, (struct sockaddr *)&sender_addr, sender_len);
            return;
        }
        */


        char buffer[MAX_LINE]; // Anpassen der Größe für die maximale Paketgröße
        struct sockaddr_in6 sender_addr;
        socklen_t sender_len = sizeof(sender_addr);

        int bytes_received = recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr *)&sender_addr, &sender_len);
        buffer[bytes_received] = '\0'; // Null-terminieren, falls es ein Textpaket ist

        printf("Debugging Empfänger: Bytes received=%d, Data=%s\n", bytes_received, buffer);
        

        // Prüfen, ob es sich um HELLO handelt
        if (strcmp(buffer, "HELLO") == 0) {
            printf("Received HELLO. Sending ACK_HELLO.\n");
            char ack_msg[] = "ACK_HELLO";
            sendto(sock, ack_msg, strlen(ack_msg), 0, (struct sockaddr *)&sender_addr, sender_len);
        } else {
            /*

            // Annehmen, dass es ein Datenpaket ist
            Packet pkt;
            memcpy(&pkt, buffer, sizeof(pkt)); 
            //memcpy(&pkt.data, buffer, sizeof(pkt.data)); // !!!
            //memcpy(pkt.data, buffer, bytes_received);
            pkt.seq_num = ntohs(pkt.seq_num); // Netzwerk-Byte-Reihenfolge zu Host-Byte-Reihenfolge
            printf("Received: SeqNum=%d\n", pkt.seq_num);

            */

            if (bytes_received < sizeof(int)) {
                printf("Invalid packet received (too small).\n");
                return;
            }


            // Extrahieren Sie die Sequenznummer (die ersten 4 Bytes im Paket)
            memcpy(&pkt.seq_num, buffer, sizeof(int));
            pkt.seq_num = ntohs(pkt.seq_num); // Konvertieren in Host-Byte-Reihenfolge

            printf("Empfänger: SeqNum=%d, Data=%s\n", pkt.seq_num, pkt.data);


            // Kopieren Sie die verbleibenden Daten in das `data`-Feld
            int data_size = bytes_received - sizeof(int);
            memcpy(pkt.data, buffer + sizeof(int), data_size);
            pkt.data[data_size] = '\0'; // Null-terminieren für sichere String-Verarbeitung
            

            printf("Received: SeqNum=%d, Data=%s\n", pkt.seq_num, pkt.data);

            if (pkt.seq_num != expected_seq) {
                printf("DEBUG: Sending NACK for SeqNum=%d\n", expected_seq);
                int nack_seq = htons(expected_seq); // Netzwerk-Byte-Reihenfolge für NACK
                sendto(sock, &nack_seq, sizeof(nack_seq), 0, (struct sockaddr *)&sender_addr, sender_len);
            } else {
                fprintf(file, "%s", pkt.data); // Speichern des Paketinhalts
                printf("Packet acknowledged: SeqNum=%d\n", pkt.seq_num);
                expected_seq++;
            }
        }

        /*
            if (pkt.seq_num == expected_seq) {
            fprintf(file, "%s", pkt.data);
            expected_seq++;
        } else {
            printf("Sending NACK: SeqNum=%d\n", expected_seq);
            sendto(sock, &expected_seq, sizeof(expected_seq), 0, (struct sockaddr *)&addr, sizeof(addr));
            
        }
        */  
    }

    fclose(file);
    close(sock);
}

/**
 * Adds a timer to the timer list. NEW
 */
void add_timer(Timer **head, int seq_num, int timeout) {
    Timer *new_timer = malloc(sizeof(Timer));
    new_timer->seq_num = seq_num;
    new_timer->timeout = timeout;
    new_timer->next = *head;
    *head = new_timer;
}

/**
 * Decrements all timers in the list by the given interval. NEW
 */
void decrement_timers(Timer **head, int interval) {
    Timer *current = *head;
    while (current) {
        current->timeout -= interval;
        if (current->timeout <= 0) {
            printf("Timer expired for seq_num=%d\n", current->seq_num);
        }
        current = current->next;
    }
}

/**
 * Removes a timer from the timer list.
 */
void remove_timer(Timer **head, int seq_num) {
    Timer **current = head;
    while (*current) {
        if ((*current)->seq_num == seq_num) {
            Timer *to_delete = *current;
            *current = (*current)->next;
            free(to_delete);
            return;
        }
        current = &((*current)->next);
    }
}


