# Computer Communications Projects  

This repository contains projects developed as part of my Computer Communications course. These projects demonstrate my understanding of networking concepts, multi-threading, and HTTP protocol implementation. All projects were developed in C and are designed to run on Linux systems.  

## Projects  

### 1. Proxy Server with Blocking Feature  
A multi-threaded proxy server that processes HTTP requests and blocks access to specific websites based on a predefined blocklist.  

#### Features:  
- **HTTP Header Parsing**: Reads HTTP headers to extract the domain name from requests.  
- **Website Blocking**: Blocks websites whose domains are listed in the blocklist file.  
- **Thread Pool**: Uses a custom thread pool for efficient dynamic thread allocation to handle multiple client connections simultaneously.  

---

### 2. Chat Room Server  
A lightweight chat room server that uses HTTP connections to facilitate communication between multiple clients.  

#### Features:  
- **Broadcast Messaging**: Relays messages from any connected client to all other clients.  
- **Concurrent Connections**: Supports multiple clients connected simultaneously.  
- **Simple Protocol**: Implements a custom lightweight protocol over HTTP for chat functionality.  

---

## Prerequisites  
- **Linux OS**: Projects were developed and tested on Linux systems.  
- **C Compiler**: GCC or any other C compiler.  

## Technologies Used  
- **Programming Language**: C  
- **Networking APIs**: POSIX sockets  
- **Threading**: POSIX Threads (pthreads)  

## Author  
**Natan**  
Feel free to reach out with any questions or suggestions.  

---  
