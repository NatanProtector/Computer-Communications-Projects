#include <malloc.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include "chatServer.h"
#include <stdlib.h>
#include <netinet/in.h>
#include <ctype.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#define connection_queue_size 32 // chosen arbitrary queue size

static int end_server = 0;

static int server_socket = 3;

void intHandler();

int is_number(const char *s);

void printUsageError();

int addMsgToConn(int sd, char* buffer, int len, conn_pool_t* pool);

char* popMsg(int sd, conn_pool_t* pool);

int freeMessagesInConnection(int sd, conn_pool_t* pool);

int freeConn(int sd, conn_pool_t* pool);

void convertToUpper(char *str);

int main (int argc, char *argv[])
{
    // Get Argument
    if (argc != 2)
        printUsageError();

    // Check if the argument is a number
    if (!is_number(argv[1]))
        printUsageError();

    // Convert the argument to a number
    char *endptr;
    long port_long = strtol(argv[1], &endptr, 10);

    // Check for conversion errors
    if (*endptr != '\0' || errno == ERANGE)
        printUsageError();

    // Convert the port to int
    int port = (int)port_long;

    // Check if port in valid range
    if (65536 < port || port < 1)
        printUsageError();

    // Mask SIGINT signal
    signal(SIGINT, intHandler);

    // Allocate memory for pool metadata
    conn_pool_t* pool = malloc(sizeof(conn_pool_t));
    memset(pool, 0, sizeof(conn_pool_t));
    initPool(pool);

    /*************************************************************/
    /* Create an AF_INET stream socket to receive incoming      */
    /* connections on                                            */
    /*************************************************************/
    struct sockaddr_in server_address;

    // Create a socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("error: socket\n");
        exit(EXIT_FAILURE);
    }

    // allow reusing the address
    int reuse = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        perror("error: setsockopt\n");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    // Initialize server_address struct
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(port);


    /*************************************************************/
    /* Set socket to be nonblocking. All of the sockets for      */
    /* the incoming connections will also be nonblocking since   */
    /* they will inherit that state from the listening socket.   */
    /*************************************************************/

    // Set socket to be non-blocking
    int enable = 1;
    if (ioctl(server_socket, FIONBIO, &enable) < 0) {
        perror("error: ioctl\n");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    /*************************************************************/
    /* Bind the socket                                           */
    /*************************************************************/

    // Bind the socket to the address and port
    if (bind(server_socket, (struct sockaddr *)&server_address, sizeof(server_address)) < 0) {
        perror("error: bind\n");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    /*************************************************************/
    /* Set the listen back log                                   */
    /*************************************************************/
    // Listen for incoming connections
    if (listen(server_socket, connection_queue_size) < 0) {  // 5 is the maximum length of the queue for pending connections
        perror("error: listen\n");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    /*************************************************************/
    /* Initialize fd_sets  			                             */
    /*************************************************************/

    // Add the listening socket to the master_read_set
    FD_SET(server_socket, &pool->ready_read_set);
    FD_CLR(0,&pool->ready_read_set); FD_CLR(1,&pool->ready_read_set); FD_CLR(2,&pool->ready_read_set);
    FD_CLR(0,&pool->ready_write_set); FD_CLR(1,&pool->ready_write_set); FD_CLR(2,&pool->ready_write_set);
    pool->maxfd = server_socket;

    /*************************************************************/
    /* Loop waiting for incoming connects, for incoming data or  */
    /* to write data, on any of the connected sockets.           */
    /*************************************************************/
    do
    {
        /**********************************************************/
        /* Copy the master fd_set over to the working fd_set.     */
        /**********************************************************/
        pool->read_set = pool->ready_read_set;
        pool->write_set = pool->ready_write_set;

        /**********************************************************/
        /* Call select() 										  */
        /**********************************************************/
        printf("Waiting on select()...\nMaxFd %d\n", pool->maxfd);
        pool->nready = select(pool->maxfd + 1, &pool->read_set, NULL, NULL, NULL);
        if (pool->nready < 0) {
            perror("error: select\n");
            /*  Clean memory:
             * */
            if (end_server == 1)
                break;
        }

        /**********************************************************/
        /* One or more descriptors are readable or writable.      */
        /* Need to determine which ones they are.                 */
        /**********************************************************/

        for (int fd = server_socket; fd < pool->maxfd +1 ; fd++)
        {
            /* Each time a ready descriptor is found, one less has  */
            /* to be looked for.  This is being done so that we     */
            /* can stop looking at the working set once we have     */
            /* found all of the descriptors that were ready         */

            /*******************************************************/
            /* Check to see if this descriptor is ready for read   */
            /*******************************************************/
            if (FD_ISSET(fd, &pool->read_set))
            {
                /****************************************************/
                /* A descriptor was found that was readable		    */
                /* if this is the listening socket, accept one      */
                /* incoming connection that is queued up on the     */
                /*  listening socket before we loop back and call   */
                /* select again. 						            */
                /****************************************************/
                // If activity on welcome socket, accept new connection
                if (fd == server_socket) {
                    printf("New incoming connection on sd %d\n", fd);
                    // Accept the connection
                    int addrlen = sizeof(server_address);
                    int new_socket = accept(server_socket, (struct sockaddr *)&server_address, (socklen_t*)&addrlen);
                    bool succesfullAccept = true;
                    if (new_socket < 0) {
                        perror("error: accept\n");
                        succesfullAccept = false;
                    }

                    // do nothing and continue if accept was unsuccessful
                    if (succesfullAccept) {

                        if (addConn(new_socket, pool) > -1) {
                            // Set the bit map
                            FD_SET(new_socket, &pool->ready_read_set);
                            FD_SET(new_socket, &pool->ready_write_set);
                        }
                    }

                }
                /****************************************************/
                /* If this is not the listening socket, an 			*/
                /* existing connection must be readable				*/
                /* Receive incoming data his socket                 */
                /****************************************************/

                else if (FD_ISSET(fd,&pool->read_set)){
                    char buffer[BUFFER_SIZE];
                    memset(buffer, 0, BUFFER_SIZE);
                    long bytes_read = read(fd, buffer, sizeof(buffer));
                    printf("Descriptor %d is readable\n", fd);
                    if (bytes_read <= 0) {
                        // Client disconnected or error occurred, remove the connection
                        removeConn(fd, pool);
                        if (bytes_read == 0)
                            printf("Connection closed for sd %d\n",fd);
                    }
                    else
                    {
                        // Null-terminate the received data
                        buffer[bytes_read] = '\0';
                        // Handle the received data (e.g., broadcast it to other clients)
                        printf("%d bytes received from sd %d\n", (int)bytes_read, fd);

                        /**********************************************/
                        /* Data was received, add msg to all other    */
                        /* connections					  			  */
                        /**********************************************/

                        convertToUpper(buffer);

                        addMsg(fd, buffer, (int)strlen(buffer),  pool);
                        // ==============================================================
                    }
                }


            } /* End of if (FD_ISSET()) */

            /*******************************************************/
            /* Check to see if this descriptor is ready for write  */
            /*******************************************************/
            if (FD_ISSET(fd, &pool->write_set)) {
                /* try to write all msgs in queue to sd */
                //writeToClient(...);
                writeToClient(fd,pool);
            }
            /*******************************************************/


        } /* End of loop through selectable descriptors */

    } while (end_server == 0);

    /*************************************************************/
    /* If we are here, Control-C was typed,						 */
    /* clean up all open connections					         */
    /*************************************************************/
    for (int fd = server_socket + 1; fd < pool->maxfd + 1 ; fd++) {
        if (FD_ISSET(fd, &pool->ready_read_set)) {
            freeConn(fd, pool);
        }
    }

    close(server_socket);

    free(pool->conn_head);

    free(pool);

    return EXIT_SUCCESS;
}


int initPool(conn_pool_t* pool) {
    // Initialize maxfd
    pool->maxfd = -1;

    // Initialize nready
    pool->nready = 0;

    // Clear the read_set, ready_read_set, write_set, and ready_write_set
    FD_ZERO(&pool->read_set);
    FD_ZERO(&pool->ready_read_set);
    FD_ZERO(&pool->write_set);
    FD_ZERO(&pool->ready_write_set);

    // Initialize the dummy head of the connection list
    pool->conn_head = malloc(sizeof(conn_t));
    if (pool->conn_head == NULL) {
        perror("error: malloc\n");
        return -1;
    }
    pool->conn_head->prev = pool->conn_head->next = pool->conn_head;
    pool->conn_head->fd = -1;
    pool->conn_head->write_msg_head = pool->conn_head->write_msg_tail = NULL;

    // Initialize nr_conns
    pool->nr_conns = 0;

    return 0;
}


int addConn(int sd, conn_pool_t* pool) {
    /*
     * 1. allocate connection and init fields
     * 2. add connection to pool
     * */

    // Allocate memory for the new connection
    conn_t* new_conn = (conn_t*)malloc(sizeof(conn_t));
    if (new_conn == NULL) {
        perror("error: malloc\n");
        return -1;
    }

    // Initialize the fields of the new connection
    new_conn->fd = sd;
    new_conn->write_msg_head = NULL;
    new_conn->write_msg_tail = NULL;

    // Add the new connection to the connection pool's linked list
    new_conn->prev = pool->conn_head->prev;
    new_conn->next = pool->conn_head;
    pool->conn_head->prev->next = new_conn;
    pool->conn_head->prev = new_conn;

    // Update the maximum file descriptor if needed
    if (sd > pool->maxfd)
        pool->maxfd = sd;

    // Increment the number of active client connections
    pool->nr_conns++;

    return 0;
}


int removeConn(int sd, conn_pool_t* pool) {
    /*
    * 1. remove connection from pool
    * 2. deallocate connection
    * 3. remove from sets
    * 4. update max_fd if needed
    */

    printf("removing connection with sd %d \n", sd);

    freeConn(sd, pool);

    FD_CLR(sd, &pool->ready_read_set);
    FD_CLR(sd, &pool->ready_write_set);

    return 0;
}

int addMsgToConn(int sd, char* buffer, int len, conn_pool_t* pool) {
    // Traverse the connection pool's linked list to find the connection with the given socket descriptor
    conn_t* current = pool->conn_head->next;
    while (current != pool->conn_head) {
        if (current->fd == sd) {
            // Found the connection

            // Allocate memory for the new message
            msg_t* new_msg = (msg_t*)malloc(sizeof(msg_t));
            if (new_msg == NULL) {
                perror("error: malloc\n");
                return -1;
            }

            // Copy the message content into the new message
            new_msg->message = (char*)malloc(len + 1); // +1 for null terminator
            if (new_msg->message == NULL) {
                perror("error: malloc\n");
                free(new_msg);
                return -1;
            }
            strncpy(new_msg->message, buffer, len);
            new_msg->message[len] = '\0'; // Null-terminate the string
            new_msg->size = len;

            // Add the new message to the connection's message queue
            new_msg->prev = current->write_msg_tail;
            new_msg->next = NULL;
            if (current->write_msg_tail != NULL) {
                current->write_msg_tail->next = new_msg;
            }
            current->write_msg_tail = new_msg;
            if (current->write_msg_head == NULL) {
                current->write_msg_head = new_msg;
            }

            return 0;
        }
        current = current->next;
    }
    return -1;
}

int addMsg(int sd,char* buffer,int len,conn_pool_t* pool) {

    /*
     * 1. add msg_t to write queue of all other connections
     * 2. set each fd to check if ready to write
     */

    // Iterate over each connection in the connection pool
    conn_t* current_conn = pool->conn_head->next;
    while (current_conn != pool->conn_head) {
        // Check if the connection's socket descriptor is different from the given socket descriptor
        if (current_conn->fd != sd) {
            // Add the message to the connection's write queue
            int result = addMsgToConn(current_conn->fd, buffer, len, pool);
            if (result != 0) {
                // Error adding message to connection
                return -1;
            }

            // Set the file descriptor to check if ready to write
            FD_SET(current_conn->fd, &pool->ready_write_set);

            // Update the maximum file descriptor if needed
            if (current_conn->fd > pool->maxfd) {
                pool->maxfd = current_conn->fd;
            }
        }
        current_conn = current_conn->next;
    }

    return 0;
}

int writeToClient(int sd,conn_pool_t* pool) {

    /*
     * 1. write all msgs in queue
     * 2. deallocate each writen msg
     * 3. if all msgs were writen successfully, there is nothing else to write to this fd... */

    // Find the connection with the given socket descriptor
    conn_t* current = pool->conn_head->next;
    while (current != pool->conn_head) {
        if (current->fd == sd) {
            // Found the connection

            // Write all messages in the connection's message queue
            char* message;
            while ((message = popMsg(sd, pool)) != NULL) {
                ssize_t bytes_written = write(sd, message, strlen(message));
                if (bytes_written < 0) {
                    // Error writing to client socket
                    perror("error: write\n");
                    return -1;
                } else if (bytes_written == 0) {
                    // Connection closed by client
                    return -1;
                }

                // Deallocate the memory associated with the message
                free(message);
            }

            // All messages were successfully written
            return 0;
        }
        current = current->next;
    }

    FD_CLR(sd, &pool->ready_write_set);

    // If the socket descriptor was not found, return -1
    return -1;
}

// Pop first message from the list that matches to socket descriptor sd.
char* popMsg(int sd, conn_pool_t* pool) {
    // Traverse the connection pool's linked list to find the connection with the given socket descriptor
    conn_t* current = pool->conn_head->next;
    while (current != pool->conn_head) {
        if (current->fd == sd) {
            // Found the connection

            // Check if there are messages in the connection's message queue
            if (current->write_msg_head == NULL) {
                // No messages in the queue
                return NULL;
            }

            // Retrieve the first message from the connection's message queue
            msg_t* first_msg = current->write_msg_head;
            char* message_content = first_msg->message;

            // Adjust the pointers of the message queue
            current->write_msg_head = first_msg->next;
            if (current->write_msg_head != NULL) {
                current->write_msg_head->prev = NULL;
            } else {
                // No more messages in the queue, update the tail pointer
                current->write_msg_tail = NULL;
            }

            // Free the memory associated with the message object
            free(first_msg);

            return message_content;
        }
        current = current->next;
    }

    // If the socket descriptor was not found, return NULL
    return NULL;
}

int freeMessagesInConnection(int sd, conn_pool_t* pool) {
    // Traverse the connection pool's linked list to find the connection with the given socket descriptor
    conn_t* current = pool->conn_head->next;
    while (current != pool->conn_head) {
        if (current->fd == sd) {
            // Found the connection

            // Free all messages in the connection's message queue
            msg_t* current_msg = current->write_msg_head;
            while (current_msg != NULL) {
                msg_t* next_msg = current_msg->next;

                free(current_msg->message);
                free(current_msg);
                current_msg = next_msg;
            }

            // Reset the message queue pointers
            current->write_msg_head = NULL;
            current->write_msg_tail = NULL;

            return 0;
        }
        current = current->next;
    }
    return -1;
}


int freeConn(int sd, conn_pool_t* pool) {
    // Traverse the connection pool's linked list to find the connection with the given socket descriptor
    conn_t* current = pool->conn_head->next;
    while (current != pool->conn_head) {
        if (current->fd == sd) {
            // Found the connection to be removed

            // Free the messages in the connection
            freeMessagesInConnection(sd, pool);

            // Adjust the pointers of neighboring connections
            current->prev->next = current->next;
            current->next->prev = current->prev;

            // Update the maximum file descriptor if needed
            if (sd == pool->maxfd) {
                // Traverse the connection pool's linked list to find the new maximum file descriptor
                int new_maxfd = server_socket;
                conn_t* temp = pool->conn_head->next;
                while (temp != pool->conn_head) {
                    if (temp->fd > new_maxfd)
                        new_maxfd = temp->fd;
                    temp = temp->next;
                }
                pool->maxfd = new_maxfd;
            }

            // Close socket
            close(current->fd);

            // Free the memory associated with the connection
            free(current);

            // Decrement the number of active client connections
            pool->nr_conns--;

            return 0;
        }
        current = current->next;
    }
    return -1;
}

void convertToUpper(char *str) {
    while (*str) {
        *str = (char)toupper((unsigned char)*str);
        str++;
    }
}

int is_number(const char *s) {
    // Iterate through each character in the string
    while (*s) {
        // If any character is not a digit, return 0 (false)
        if (!isdigit(*s))
            return 0;
        s++;
    }
    // If all characters are digits, return 1 (true)
    return 1;
}

void printUsageError() {
    printf("Usage: server <port>");
    exit(EXIT_FAILURE);
}

void intHandler() {
    end_server = 1;
}