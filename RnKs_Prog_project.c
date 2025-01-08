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

// Funktion, die ein Paket an die Multicast-Adresse sendet
void send_packet(int sockfd, struct sockaddr_in6 *dest_addr, Packet *pkt) {
    ssize_t sent_size = sendto(sockfd, pkt, sizeof(*pkt), 0, (struct sockaddr *)dest_addr, sizeof(*dest_addr));
    if (sent_size < 0) {
        perror("Send error");
        exit(1);
    }
}

// Hauptsende-Funktion
void send_file(const char *filename, const char *multicast_ip, int port, int window_size) {
    // Datei öffnen
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("File open error");
        return;
    }

    // Socket für UDPv6 erstellen
    int sockfd = socket(AF_INET6, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Socket creation error");
        fclose(file);
        return;
    }

    // Zieladresse für Multicast festlegen
    struct sockaddr_in6 dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin6_family = AF_INET6;
    dest_addr.sin6_port = htons(port);
    inet_pton(AF_INET6, multicast_ip, &dest_addr.sin6_addr);

    Packet pkt;
    int seq_num = 0;
    while (fgets(pkt.data, MAX_PACKET_SIZE, file)) {
        pkt.seq_num = seq_num++;
        send_packet(sockfd, &dest_addr, &pkt);
        printf("Sent packet: SeqNum=%d\n", pkt.seq_num);
        // Warten auf Bestätigung oder Timeout (hier einfach simuliert)
        sleep(1);
    }

    fclose(file);
    close(sockfd);
}

int main(int argc, char *argv[]) {
    if (argc != 5) {
        fprintf(stderr, "Usage: %s <filename> <multicast-ip> <port> <window-size>\n", argv[0]);
        return 1;
    }

    const char *filename = argv[1];
    const char *multicast_ip = argv[2];
    int port = atoi(argv[3]);
    int window_size = atoi(argv[4]);

    send_file(filename, multicast_ip, port, window_size);
    return 0;
}
