#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#define MAX_PACKET_SIZE 1024

// Struktur für das Paket
typedef struct {
    int seq_num;
    char data[MAX_PACKET_SIZE];
} Packet;

// Funktion zum Empfangen eines Pakets
void receive_packet(int sockfd, struct sockaddr_in6 *src_addr, Packet *pkt) {
    socklen_t addr_len = sizeof(*src_addr);
    ssize_t recv_size = recvfrom(sockfd, pkt, sizeof(*pkt), 0, (struct sockaddr *)src_addr, &addr_len);
    if (recv_size < 0) {
        perror("Receive error");
        exit(1);
    }
}

// Funktion zum Senden eines NACKs
void send_nack(int sockfd, struct sockaddr_in6 *dest_addr, int seq_num) {
    char nack_msg[100];
    snprintf(nack_msg, sizeof(nack_msg), "NACK %d", seq_num);
    ssize_t sent_size = sendto(sockfd, nack_msg, strlen(nack_msg) + 1, 0, (struct sockaddr *)dest_addr, sizeof(*dest_addr));
    if (sent_size < 0) {
        perror("Send NACK error");
        exit(1);
    }
    printf("Sent NACK for SeqNum=%d\n", seq_num);
}

// Hauptempfänger-Funktion
void receive_file(int port) {
    // Socket für UDPv6 erstellen
    int sockfd = socket(AF_INET6, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Socket creation error");
        return;
    }

    // Adresse für den Empfänger
    struct sockaddr_in6 addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(port);
    addr.sin6_addr = in6addr_any;

    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("Bind error");
        close(sockfd);
        return;
    }

    Packet pkt;
    struct sockaddr_in6 src_addr;
    int expected_seq_num = 0;  // Erwartete Sequenznummer

    while (1) {
        // Paket empfangen
        receive_packet(sockfd, &src_addr, &pkt);
        printf("Received packet: SeqNum=%d\n", pkt.seq_num);

        // Überprüfen, ob die Sequenznummer korrekt ist
        if (pkt.seq_num == expected_seq_num) {
            printf("Packet %d received correctly.\n", pkt.seq_num);
            expected_seq_num++;  // Nächste erwartete Sequenznummer erhöhen
        } else {
            // NACK senden, wenn die Sequenznummer nicht korrekt ist
            printf("Out of order packet received: expected %d, got %d. Sending NACK.\n", expected_seq_num, pkt.seq_num);
            send_nack(sockfd, &src_addr, pkt.seq_num);
        }
    }

    close(sockfd);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    receive_file(port);

    return 0;
}
