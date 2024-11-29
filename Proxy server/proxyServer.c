#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdbool.h>
#include <netdb.h>
#include "threadpool.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>

#define BIG_BUFFER_SIZE (8*1024)
#define BUFFER_SIZE (1024)
#define MEDIUM_BUFFER_SIZE 512
#define SMALL_BUFFER_SIZE 128
#define number_of_arguments 4



/** PROBLEMS
 * - cononection timeout is very long, I am assuming that it is not a problem as we were instructed that adding a connection timeout is unnecessary
 * */

// For testing ===================================================
// 1. http://jsonplaceholder.typicode.com/posts/1
// 2. http://www.josephwcarrillo.com/JosephWhitfieldCarrillo.jpg
// 3. http://www.josephwcarrillo.com/
// 4. http://www.josephwcarrillo.com/photos.html
// 5. http://placekitten.com/200/300
// 6. http://www.josephwcarrillo.com/news.html
// 7. http://www.josephwcarrillo.com/index.html
// ================================================================

// Struct to hold client socket file descriptor
typedef struct {
    int client_socket;
    char** filter;
    int filter_len;
} ClientInfo;

void code_to_str(int code, char* buffer, char* message_buffer);
void generate_error_response(char *buffer, int code);
void set_connection_to_close(char *request);
void handle_client(void *arg);
int handle_client_wrapper(void *arg);
void print_usage_error_and_quit();
void parse_arguments(int argc, char *argv[], long *port, long *pool_size, long *max_number_of_requests, char **filter_absolute_address);
char** parseFile(const char* filepath, int* numLines);
void handle_error(const char *msg, char** filter, int filter_len, int server_fd, threadpool* tp);
void getPortFromName(const char *hostname_with_port, in_port_t *port);
bool validateAndParseRequest(const char *request, char *method, char *path, char* protocol, char *host);
bool compareToFilter(const char **ipArr, int ip_size, const char **filter, int filter_size, char* hostname);
bool is_socket_closed(int sockfd);
bool generate_response(int status_code, char *response_buffer, char *request_buffer, struct hostent* server_info, int server_port, int client_socket);

int main(int argc, char* argv[]) {

    // Initiating variables for arguments
    long port, pool_size, max_number_of_requests;
    char *filter_absolute_address;

    // parse arguments
    parse_arguments(argc, argv, &port, &pool_size, &max_number_of_requests, &filter_absolute_address);

    // Create a thread pool with 4 threads
    threadpool *tp = create_threadpool((int) pool_size);

    // Check that thread was created correctly:
    if (tp == NULL) {
        printf("error: create_threadpool\n");
        exit(EXIT_FAILURE);
    }

    // parse filter file into array
    int filter_len;
    char **filter = parseFile(filter_absolute_address, &filter_len);

    // check if filter parsing was correct
    if (filter == NULL) {
        destroy_threadpool(tp);
        exit(EXIT_FAILURE);
    }

    // Initiating variables for socket info
    int server_fd, client_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    // Creating socket file descriptor for IPv4, TCP connection
    if ((server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == 0)
        handle_error("error: socket\n", filter, filter_len, -1, tp);

    // Set socket options
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)))
        handle_error("error: setsockopt\n", filter, filter_len, server_fd, tp);

    // Specify address family of Internet Protocol v4 addresses
    address.sin_family = AF_INET;
    // Set the IP address of the socket to indicate that the socket can accept connections
    // from any network interface on the system.
    address.sin_addr.s_addr = INADDR_ANY;

    // Set the port number of the socket to the specified port.
    address.sin_port = htons(port);

    // Forcefully attaching socket to the port 8080
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
        handle_error("error: bind\n", filter, filter_len, server_fd, tp);

    // Start listening to server
    if (listen(server_fd, (int) max_number_of_requests) < 0) // Listen for a single connection
        handle_error("error: listen\n", filter, filter_len, server_fd, tp);


    // Dispatch tasks to the thread pool
    for (int i = 0; i < max_number_of_requests; i++) {

        // Create the socket for the client
        if ((client_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0)
            handle_error("error: accept\n", filter, filter_len, server_fd, tp);

        // Allocate memory for client_info
        ClientInfo *client_info = (ClientInfo *)malloc(sizeof(ClientInfo));
        if (client_info == NULL)
            handle_error("error: malloc\n", filter, filter_len, server_fd, tp);

        // Add socket to client info
        client_info->client_socket = client_socket;

        // Add filter array to the threads and the length
        client_info->filter = filter;
        client_info->filter_len = filter_len;

        // Dispatch task to handle the client connection
        dispatch(tp, (dispatch_fn) handle_client_wrapper, client_info);
    }

    // Destroy the thread pool
    destroy_threadpool(tp);

    // close server socket
    close(server_fd);
    
    // Free allocated memory for filter
    for (int i = 0; i < filter_len; ++i)
        free(filter[i]);
    free(filter);

    return EXIT_SUCCESS;
}

// Function to handle a client connection
void handle_client(void *arg) {

    // Retrieve argument
    ClientInfo *client_info = (ClientInfo *)arg;
    const int client_socket = client_info->client_socket;
    const int filter_len = client_info->filter_len;
    const char** filter = (const char **) client_info->filter;

    // Initiate variable for request buffer
    char request_buffer[BIG_BUFFER_SIZE] = {0};
    memset(request_buffer,0,BIG_BUFFER_SIZE);
    ssize_t valread;

    // Initiate variable for response buffer
    char response[BIG_BUFFER_SIZE];
    memset(response,0,BIG_BUFFER_SIZE);

    // Receive message from client
    valread = read(client_socket, request_buffer, BIG_BUFFER_SIZE);
    if (valread <= 0) {
        perror("error: read\n");
        free(client_info); // free alloc
        close(client_socket); // Close the socket
        return; // Exit the thread
    }


    // Initiate variables for parsing the http request
    char method[SMALL_BUFFER_SIZE], path[MEDIUM_BUFFER_SIZE], protocol[SMALL_BUFFER_SIZE], host[MEDIUM_BUFFER_SIZE];
    in_port_t port = 80;
    int status_code = 200;

    memset(method,0, SMALL_BUFFER_SIZE);
    memset(path,0, MEDIUM_BUFFER_SIZE);
    memset(protocol,0, SMALL_BUFFER_SIZE);
    memset(host,0, MEDIUM_BUFFER_SIZE);

    // Parse for method, path, protocol, host
    bool parsing_successful = validateAndParseRequest(request_buffer, method, path, protocol, host);

    // Get the port
    getPortFromName(host, &port);

    // if parsed successfully check for supported method
    if (parsing_successful) {
        if (strcmp(method,"GET") != 0)// If method not GET
            status_code = 501;
    } else {
        status_code = 400;
    }

    struct hostent* server_info = NULL;
    if (status_code == 200) { // If status code was not changed

        /* Use gethostbyname to translate host name to network byte order ip address */
        server_info = gethostbyname(host);

        if (server_info == NULL) { // If DNS servers does not find the ip for the host
            status_code = 404;
        }
        else {

            // Check if the host gets filtered ==========================================

            // Initilize array for ip addreses
            char* ip_addresses[SMALL_BUFFER_SIZE];
            int len = 0;

            // Loop through each IP address and store it in the array
            for (int i = 0; server_info->h_addr_list[i] != NULL && i < SMALL_BUFFER_SIZE; i++) {

                ip_addresses[i] = malloc(INET_ADDRSTRLEN * sizeof(char));
                if (ip_addresses[i] == NULL) {
                    for (int k = i - 1 ; k >= 0 ; k--) // free previous ip addresses
                        free(ip_addresses[i]);
                    perror("error: malloc\n");
                    free(client_info); // free alloc
                    close(client_socket); // Close the socket
                    return;   // Exit the thread
                }

                struct in_addr addr;

                memcpy(&addr, server_info->h_addr_list[i], sizeof(struct in_addr));
                inet_ntop(AF_INET, &addr, ip_addresses[i], INET_ADDRSTRLEN);
                len++;
            }

            // Compare ip array and hostname to filter
            bool filtered = compareToFilter((const char **) ip_addresses, len, filter, filter_len, host);
            if (filtered)
                status_code = 403;


            // Free memory allocated for IP address strings
            for (int i = 0; i < len; i++) {
                free(ip_addresses[i]);
            }
            // filtering ===============================================================
        }
    }

    // Generate and send response based on the resulting status code
    generate_response(status_code, response,request_buffer, server_info, port, client_socket);

    // Close the socket
    close(client_socket);

    // Free memory allocated for client_info
    free(client_info);
}

// Function to generate response based on status code
bool generate_response(int status_code, char *response_buffer, char *request_buffer, struct hostent* server_info, int server_port, const int client_socket) {
    long bytes_sent_to_dest, bytes_received;
    int sockfd;
    struct sockaddr_in server_addr;

    if (status_code == 200) {
        // Create socket
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            perror("error: socket\n");
            return false;
        }

        // Fill in the server address structure
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(server_port);
        memcpy(&server_addr.sin_addr.s_addr, server_info->h_addr, server_info->h_length);

        // Connect to server
        if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            perror("error: connect\n");
            close(sockfd);
            return false;
        }

        // Set connection to closed
        set_connection_to_close(request_buffer);

        // Forward the request to the server
        bytes_sent_to_dest = send(sockfd, request_buffer, strlen(request_buffer), 0);
        if (bytes_sent_to_dest < 0) {
            perror("error: send\n");
            close(sockfd);
            return false;
        }



        // Transmit response back to client while there is still data left
        while (1) {
            bytes_received = recv(sockfd, response_buffer, BIG_BUFFER_SIZE, 0);
            if (bytes_received < 0) {
                perror("error: recv\n");
                close(sockfd);
                return false;
            } else if (bytes_received == 0) {
                close(sockfd);
                return false;
            }

            if (is_socket_closed(client_socket)) {
                close(sockfd);
                return false;
            }

            // Send response back to client
            ssize_t bytes_sent = send(client_socket, response_buffer, bytes_received, 0);
            if (bytes_sent < 0) {
                // Check for broken pipe error
                if (errno == EPIPE) {
                    // Handle broken pipe gracefully
                    break;
                } else {
                    perror("error: send\n");
                    close(sockfd);
                    return false;
                }
            }
        }

        // Close the detination socket
        close(sockfd);
    } else {
        generate_error_response(response_buffer, status_code);
    }
    return true;
}

// Function to check if a socket is still open
bool is_socket_closed(int sockfd) {
    int error = 0;
    socklen_t len = sizeof(error);
    int ret = getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &len);
    if (ret != 0) {
        // Error occurred while checking socket status
        perror("error: getsockopt\n");
        return true;
    }
    if (error != 0) {
        // Socket is closed or has encountered an error
        return true;
    }
    // Socket is still open
    return false;
}

// Ceck if an ip address is in a given network address and subnet
bool is_ip_in_network(const char *ip_with_mask, const char *ip_to_check) {
    // Split IP address and mask
    char *ip_mask_copy = strdup(ip_with_mask);
    char *ip_str = strtok(ip_mask_copy, "/");
    char *mask_str = strtok(NULL, "/");

    // If no mask specified, assume /32
    if (mask_str == NULL)
        mask_str = "32";

    // Convert IP address and mask to binary format
    struct in_addr ip, network, mask;
    if (inet_pton(AF_INET, ip_str, &ip) != 1 ||
        inet_pton(AF_INET, "255.255.255.255", &mask) != 1) {
        free(ip_mask_copy);
        return false;
    }

    uint32_t prefix_bits = strtoul(mask_str, NULL, 10);
    mask.s_addr = htonl((0xFFFFFFFFU << (32 - prefix_bits)));

    // Calculate network address
    network.s_addr = ip.s_addr & mask.s_addr;

    // Convert IP address to binary format for comparison
    struct in_addr ip_check;
    if (inet_pton(AF_INET, ip_to_check, &ip_check) != 1) {
        free(ip_mask_copy);
        return false;
    }

    // Check if IP address is in the network
    bool result = ((ip_check.s_addr & mask.s_addr) == network.s_addr);

    free(ip_mask_copy);
    return result;
}

// Check is string is an ip BASED ON ASSIGNMENT ASSUMPTION WHERE HOST NAMES DO NOT START WITH NUMBER
bool is_ip_address(const char *str) {
    // Check if the first character is a digit
    if (isdigit(str[0])) {
        return true; // First character indicates an IP address
    }
    return false;
}

// Compre array of ip addresses and host to the filter file contents
bool compareToFilter(const char **ipArr, int ip_size, const char **filter, int filter_size, char* hostname) {
    for (int i = 0; i < ip_size; i++) {
        for (int j = 0; j < filter_size; j++) {

            bool result = false;
            if (is_ip_address(filter[j])) {
                result = is_ip_in_network(filter[j], ipArr[i]);
            } else {
                result = strcmp(hostname, filter[j]) == 0? true: false;
            }

            if (result)
                return true;

        }
    }
    return false;
}

// Function to validate and parse an HTTP request
bool validateAndParseRequest(const char *request, char *method, char *path, char* protocol, char *host) {
    // Initiate variables for parsing the HTTP request
    char *token;
    char *saveptr;
    char *request_copy = strdup(request); // Make a copy of the request to avoid modifying the original

    // Extract method
    token = strtok_r(request_copy, " ", &saveptr);
    if (token == NULL)
        return false;
    strcpy(method, token);

    // Extract path
    token = strtok_r(NULL, " ", &saveptr);
    if (token == NULL)
        return false;
    strcpy(path, token);

    // Extract protocol
    token = strtok_r(NULL, "\r\n", &saveptr);
    if (token == NULL)
        return false;
    strcpy(protocol, token);

    // Check if the protocol is one of the HTTP versions
    if (strcmp(protocol, "HTTP/1.0") != 0 && strcmp(protocol, "HTTP/1.1") != 0 && strcmp(protocol, "HTTP/2.0") != 0)
        return false;

    // Extract host
    token = strstr(request, "Host: ");
    if (token == NULL)
        return false;
    sscanf(token, "Host: %s", host);

    free(request_copy); // Free the allocated memory for the copy
    return true;
}

// Parse arguments from the main
void parse_arguments(int argc, char *argv[], long *port, long *pool_size, long *max_number_of_requests, char **filter_absolute_address) {
    if (argc != number_of_arguments + 1)
        print_usage_error_and_quit();

    // Parse and validate port
    char *endptr;
    *port = strtol(argv[1], &endptr, 10);
    if (*endptr != '\0' || *port <= 0 || *port > 65535)
        print_usage_error_and_quit();

    // Parse and validate pool size
    *pool_size = strtol(argv[2], &endptr, 10);
    if (*endptr != '\0' || *pool_size <= 0)
        print_usage_error_and_quit();

    // Parse and validate max number of requests
    *max_number_of_requests = strtol(argv[3], &endptr, 10);
    if (*endptr != '\0' || *max_number_of_requests <= 0)
        print_usage_error_and_quit();

    // Assign filter absolute address
    *filter_absolute_address = argv[4];
}

// Get contents from a file and return array of lines seperated by new space
char** parseFile(const char* filepath, int* numLines) {
    // Opening file
    FILE* file = fopen(filepath, "r");
    if (!file) {
        perror("error: open\n");
        return NULL;
    }
    // Creating array of lines from the filter file
    char** lines = (char**)malloc(BUFFER_SIZE * sizeof(char*));
    if (!lines) {
        fclose(file);
        perror("error: malloc\n");
        return NULL;
    }

    char buffer[BIG_BUFFER_SIZE];
    int count = 0;
    while (fgets(buffer, BIG_BUFFER_SIZE, file)) {
        if (count >= BIG_BUFFER_SIZE) {
            fprintf(stderr, "Too many lines in the file\n");
            break;
        }
        // Remove the newline character from the end of the line
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len - 1] == '\n')
            buffer[len - 1] = '\0';
        lines[count] = strdup(buffer);
        if (!lines[count]) {
            perror("error: malloc\n");
            fclose(file);
            for (int i = 0; i < count; ++i)
                free(lines[i]);
            free(lines);
            return NULL;
        }
        count++;
    }

    fclose(file);
    *numLines = count;
    return lines;
}

// Error handling function to clean the main
void handle_error(const char *msg, char** filter, int filter_len, int server_fd, threadpool* tp) {
    perror(msg); // Print the system error message
    // Close the server socket if it's open
    if (server_fd != -1)
        close(server_fd);
    // Destroy the thread pool if it's created
    if (tp != NULL)
        destroy_threadpool(tp);
    // Free memory allocated for the filter
    if (filter != NULL) {
        for (int i = 0; i < filter_len; ++i)
            free(filter[i]);
        free(filter);
    }
    exit(EXIT_FAILURE); // Exit the program with a failure status
}

// wrong usage error handler.
void print_usage_error_and_quit() {
    printf("Usage: proxyServer <port> <pool-size> <max-number-of-request> <filter>\n");
    exit(EXIT_FAILURE);
}

// Wrapper function to match the expected function signature of dispatch_fn
int handle_client_wrapper(void *arg) {
    // Call the original handle_client function
    handle_client(arg);
    // Return a dummy value to match the expected function signature
    return 0;
}

// get the port number.
void getPortFromName(const char *hostname_with_port, in_port_t *port) {
    const char *colon = strrchr(hostname_with_port, ':'); // Find last occurrence of colon
    if (colon == NULL) { // no port found
        *port = 80; // Default port 80
    } else {
        // Convert port string to integer using strtol
        char *endptr;
        long int port_num = strtol(colon + 1, &endptr, 10);
        if (*endptr != '\0' || port_num < 1 || port_num > 65535) {
            // Invalid port format or out of range, set to default port 80
            *port = 80;
        } else {
            *port = (in_port_t)port_num;
        }
    }
}

void code_to_str(int code, char* buffer, char* message_buffer) {
    switch (code) {
        case 400:
            sprintf(buffer, "400 Bad Request");
            sprintf(message_buffer, "Bad Request.");
            break;
        case 403:
            sprintf(buffer, "403 Forbidden");
            sprintf(message_buffer, "Access denied.");
            break;
        case 404:
            sprintf(buffer, "404 Not Found");
            sprintf(message_buffer, "File not found.");
            break;
        case 500:
            sprintf(buffer, "500 Internal Server Error");
            sprintf(message_buffer, "Some server side error.");
            break;
        case 501:
            sprintf(buffer, "501 Not supported");
            sprintf(message_buffer, "Method is not supported.");
            break;
        default:
            printf("code unsupported\n");
            break;
    }
}


void generate_error_response(char *buffer, int code) {
    time_t now;
    struct tm tm;
    char date_string[64];
    char html_body[BUFFER_SIZE];
    char message[MEDIUM_BUFFER_SIZE];
    char code_str[SMALL_BUFFER_SIZE];

    code_to_str(code,code_str, message);

    sprintf(html_body,"<HTML><HEAD><TITLE>%s</TITLE></HEAD>\n<BODY><H4>%s</H4>\n%s\n</BODY></HTML>",code_str,code_str ,message);

    // Get current time
    time(&now);
    gmtime_r(&now, &tm);

    // Format the date string
    strftime(date_string, sizeof(date_string), "%a, %d %b %Y %H:%M:%S GMT", &tm);

    // Generate the HTTP response
    sprintf(buffer,
            "HTTP/1.1 %s\r\n"
            "Server: webserver/1.0\r\n"
            "Date: %s\r\n"
            "Content-Type: text/html\r\n"
            "Content-Length: %zu\r\n"
            "Connection: close\r\n"
            "\r\n"
            "%s",
            code_str,
            date_string,
            strlen(html_body),
            html_body);
}

void set_connection_to_close(char *request) {
    // Check if the request contains a header for closing the connection
    char *connection_close_header = strstr(request, "Connection: close");

    // Check if the request contains a header for keeping the connection open
    char *connection_keep_alive_header = strstr(request, "Connection: keep-alive");

    if (connection_close_header) {
        // Request already set to close, do nothing
        return;
    }

    if (connection_keep_alive_header) {
        // Replace "Connection: keep-alive" with "Connection: close"
        char *end_of_line = strstr(connection_keep_alive_header, "\r\n");
        strcpy(connection_keep_alive_header, "Connection: close");
        memmove(connection_keep_alive_header + strlen("Connection: close"), end_of_line, strlen(end_of_line) + 1);
    } else {
        // Add "Connection: close" header
        char *end_of_headers = strstr(request, "\r\n\r\n");
        if (end_of_headers) {
            memmove(end_of_headers + 4 + strlen("Connection: close"), end_of_headers + 4, strlen(end_of_headers) + 1);
            memmove(end_of_headers + 4, "Connection: close\r\n", strlen("Connection: close\r\n"));
        }
    }
}

