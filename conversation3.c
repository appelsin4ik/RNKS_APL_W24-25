#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <poll.h>  // Poll Header

#define MAX_LINE_LENGTH 1024
#define MAX_WINDOW_SIZE 10
#define TIMER_INTERVAL 300 // in milliseconds
#define RETRANSMISSION_TIMEOUT 900 // 3 * TIMER_INTERVAL
#define MAX_RETRANSMISSIONS 5

typedef struct {
    int seq_num;
    char data[MAX_LINE_LENGTH];
    int acked;
    int timer;
} Packet;

void send_packet(int sockfd, struct sockaddr_in6 *dest_addr, Packet *packet) {
    char buffer[MAX_LINE_LENGTH + 10];
    snprintf(buffer, sizeof(buffer), "%d:%s", packet->seq_num, packet->data);
    sendto(sockfd, buffer, strlen(buffer), 0, (struct sockaddr *)dest_addr, sizeof(*dest_addr));
    printf("Sent packet %d: %s\n", packet->seq_num, packet->data);
}

int sender(const char *filename, const char *multicast_address, int port, int window_size) {
    if (window_size < 1 || window_size > MAX_WINDOW_SIZE) {
        fprintf(stderr, "Window size must be between 1 and %d\n", MAX_WINDOW_SIZE);
        return EXIT_FAILURE;
    }

    // Open the file for reading
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("Failed to open file");
        return EXIT_FAILURE;
    }

    // Create UDP socket
    int sockfd = socket(AF_INET6, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        fclose(file);
        return EXIT_FAILURE;
    }

    // Configure destination address
    struct sockaddr_in6 dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin6_family = AF_INET6;
    dest_addr.sin6_port = htons(port);

    if (inet_pton(AF_INET6, multicast_address, &dest_addr.sin6_addr) <= 0) {
        perror("Invalid multicast address");
        close(sockfd);
        fclose(file);
        return EXIT_FAILURE;
    }
    

    int loop = 1;
    if (setsockopt(sockfd, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, &loop, sizeof(loop)) < 0) {
    perror("Error setting multicast loop option");
    close(sockfd);
    fclose(file);
    return EXIT_FAILURE;
}


    /*
    

    if (bind(sockfd, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) < 0) {
    perror("Bind failed");
    close(sockfd);
    return EXIT_FAILURE;
    }
    */
    

    Packet window[MAX_WINDOW_SIZE];
    int base = 0, next_seq_num = 0;
    int retransmit_count[MAX_WINDOW_SIZE] = {0};  // Array to track retransmissions for each packet
    struct pollfd fds[1];
    fds[0].fd = sockfd;
    fds[0].events = POLLIN;

    // Main loop
    while (1) {
        // Fill the window with new packets if possible
        while (next_seq_num < base + window_size) {
            if (fgets(window[next_seq_num % MAX_WINDOW_SIZE].data, MAX_LINE_LENGTH, file)) {
                window[next_seq_num % MAX_WINDOW_SIZE].seq_num = next_seq_num;
                window[next_seq_num % MAX_WINDOW_SIZE].acked = 0;
                window[next_seq_num % MAX_WINDOW_SIZE].timer = RETRANSMISSION_TIMEOUT;
                send_packet(sockfd, &dest_addr, &window[next_seq_num % MAX_WINDOW_SIZE]);
                next_seq_num++;
            } else {
                if (next_seq_num == base) {
                    break;
                }
            }
        }

        // Wait for events on the socket using poll
        int poll_result = poll(fds, 1, TIMER_INTERVAL); // Timeout is in milliseconds
        if (poll_result < 0) {
            perror("Poll error");
            break;
        } else if (poll_result == 0) {
            // Timeout expired, handle retransmissions
            for (int i = base; i < next_seq_num; i++) {
                if (!window[i % MAX_WINDOW_SIZE].acked) {
                    window[i % MAX_WINDOW_SIZE].timer -= TIMER_INTERVAL;
                    if (window[i % MAX_WINDOW_SIZE].timer <= 0) {
                        if (retransmit_count[i % MAX_WINDOW_SIZE] < MAX_RETRANSMISSIONS) {
                            send_packet(sockfd, &dest_addr, &window[i % MAX_WINDOW_SIZE]);
                            window[i % MAX_WINDOW_SIZE].timer = RETRANSMISSION_TIMEOUT;
                            retransmit_count[i % MAX_WINDOW_SIZE]++;
                            printf("Retransmitting packet %d (attempt %d)\n", window[i % MAX_WINDOW_SIZE].seq_num, retransmit_count[i % MAX_WINDOW_SIZE]);
                        } else {
                            printf("Max retransmissions reached for packet %d\n", window[i % MAX_WINDOW_SIZE].seq_num);
                        }
                    }
                }
            }
        } else {
            // Handle incoming NACKs or ACKs
            char buffer[256];
            struct sockaddr_in6 src_addr;
            socklen_t src_addr_len = sizeof(src_addr);
            int len = recvfrom(sockfd, buffer, sizeof(buffer) - 1, 0, (struct sockaddr *)&src_addr, &src_addr_len);
            if (len > 0) {
                buffer[len] = '\0';
                int nack_seq_num = atoi(buffer);
                printf("Received NACK for packet %d\n", nack_seq_num);
                if (nack_seq_num >= base && nack_seq_num < next_seq_num) {
                    send_packet(sockfd, &dest_addr, &window[nack_seq_num % MAX_WINDOW_SIZE]);
                    window[nack_seq_num % MAX_WINDOW_SIZE].timer = RETRANSMISSION_TIMEOUT;
                }
            }
        }

        // Slide the window if possible
        while (window[base % MAX_WINDOW_SIZE].acked) {
            base++;
        }

        // Exit condition: All packets sent and acknowledged
        if (base == next_seq_num && feof(file)) {
            break;
        }
    }

    fclose(file);
    close(sockfd);
    printf("File transmission complete.\n");
    return EXIT_SUCCESS;
}

int receiver(const char *filename, const char *multicast_address, int port) {
    // Open the file for writing
    FILE *file = fopen(filename, "w");
    if (!file) {
        perror("Failed to open file");
        return EXIT_FAILURE;
    }

    // Create UDP socket
    int sockfd = socket(AF_INET6, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        fclose(file);
        return EXIT_FAILURE;
    }

    // Configure local address
    struct sockaddr_in6 local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin6_family = AF_INET6;
    local_addr.sin6_port = htons(port);
    local_addr.sin6_addr = in6addr_any;

    if (bind(sockfd, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0) {
        perror("Bind failed");
        close(sockfd);
        fclose(file);
        return EXIT_FAILURE;
    }

    // Join multicast group
    struct ipv6_mreq group;
    if (inet_pton(AF_INET6, multicast_address, &group.ipv6mr_multiaddr) <= 0) {
        perror("Invalid multicast address");
        close(sockfd);
        fclose(file);
        return EXIT_FAILURE;
    }
    group.ipv6mr_interface = 0; // Default interface
    if (setsockopt(sockfd, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP, &group, sizeof(group)) < 0) {
        perror("Failed to join multicast group");
        close(sockfd);
        fclose(file);
        return EXIT_FAILURE;
    }

    int reuse = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
    perror("setsockopt failed");
    close(sockfd);
    return EXIT_FAILURE;
    }

    Packet buffer[MAX_WINDOW_SIZE] = {0};
    int expected_seq_num = 0;

    // Start message to indicate waiting state
    printf("Receiver started. Waiting for packets...\n");

    // Main loop to receive packets
    while (1) {
        char recv_buffer[MAX_LINE_LENGTH + 10];
        struct sockaddr_in6 src_addr;
        socklen_t src_addr_len = sizeof(src_addr);
        int len = recvfrom(sockfd, recv_buffer, sizeof(recv_buffer) - 1, 0, (struct sockaddr *)&src_addr, &src_addr_len);

        if (len > 0) {
            recv_buffer[len] = '\0';
            int seq_num;
            char data[MAX_LINE_LENGTH];
            
            // Updated sscanf format string for safer parsing
            if (sscanf(recv_buffer, "%d:%1023[^\n]", &seq_num, data) == 2) {
                if (seq_num == expected_seq_num) {
                    fprintf(file, "%s", data);
                    printf("Received packet %d: %s\n", seq_num, data);
                    expected_seq_num++;

                } else {
                    printf("Out-of-order packet %d received, expected %d\n", seq_num, expected_seq_num);
                    send_packet(sockfd, &src_addr, &buffer[expected_seq_num % MAX_WINDOW_SIZE]);
                }
            } else {
                printf("Malformed packet received: %s\n", recv_buffer);
            }
        }
    }

    close(sockfd);
    fclose(file);
    printf("File reception complete.\n");
    return EXIT_SUCCESS;
}

void print_usage(const char *prog_name) {
    printf("Usage: %s <role> <filename> <multicast_address> <port> [window_size]\n", prog_name);
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
    if (argc < 5 || argc > 6) {
        print_usage(argv[0]);
    }

    const char *role = argv[1];
    const char *filename = argv[2];
    const char *multicast_address = argv[3];
    int port = atoi(argv[4]);
    
    // Fenstergröße aus den Argumenten extrahieren und validieren
    int window_size = 1; // Default-Wert
    if (argc == 6) {
        window_size = atoi(argv[5]);
        if (window_size < 1 || window_size > MAX_WINDOW_SIZE) {
            fprintf(stderr, "Fehler: Fenstergröße muss zwischen 1 und %d liegen.\n", MAX_WINDOW_SIZE);
            return EXIT_FAILURE;
        }
    }

    // Überprüfen der Portnummer
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Fehler: Ungültiger Port.\n");
        return EXIT_FAILURE;
    }

    // WSAInitialisierung unter Windows
    #ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "WSAStartup fehlgeschlagen\n");
        return EXIT_FAILURE;
    }
    #endif

    if (strcmp(role, "sender") == 0) {
        return sender(filename, multicast_address, port, window_size);
    } else if (strcmp(role, "receiver") == 0) {
        return receiver(filename, multicast_address, port);
    } else {
        fprintf(stderr, "Unbekannte Rolle: %s\n", role);
        print_usage(argv[0]);
    }

    // WSACleanUp für Windows
    #ifdef _WIN32
    WSACleanup();
    #endif

    return EXIT_SUCCESS;
}