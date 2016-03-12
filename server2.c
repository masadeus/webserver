//
// server.c
//
// David J. Malan
// malan@harvard.edu
//

// feature test macro requirements
#define _GNU_SOURCE
#define _XOPEN_SOURCE 700
#define _XOPEN_SOURCE_EXTENDED

// limits on an HTTP request's size, based on Apache's
// http://httpd.apache.org/docs/2.2/mod/core.html
#define LimitRequestFields 50
#define LimitRequestFieldSize 4094
#define LimitRequestLine 8190

// number of octets for buffered reads
#define OCTETS 512

// header files
#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

// types
typedef char octet;

// prototypes
bool connected(void);
bool error(unsigned short code);
void handler(int signal);
ssize_t load(void);
const char* lookup(const char* extension);
ssize_t parse(void);
void reset(void);
void start(short port, const char* path);
void stop(void);

// server's root
char* root = NULL;

// file descriptor for sockets. Computer and Server file descriptor
int cfd = -1, sfd = -1;

// buffer for request
octet* request = NULL;

// FILE pointer for files
FILE* file = NULL;

// buffer for response-body
octet* body = NULL;

int main(int argc, char* argv[])
{
    // a global variable defined in errno.h that's "set by system 
    // calls and some library functions [to a nonzero value]
    // in the event of an error to indicate what went wrong"
    errno = 0;

    // default to a random port
    int port = 0;

    // usage
    const char* usage = "Usage: server [-p port] /path/to/root";

    // parse command-line arguments
    int opt;
    while ((opt = getopt(argc, argv, "hp:")) != -1)
    {
        switch (opt)
        {
            // -h
            case 'h':
                printf("%s\n", usage);
                return 0;

            // -p port
            case 'p':
                port = atoi(optarg);
                break;
        }
    }

    // ensure port is a non-negative short and path to server's root is specified
    if (port < 0 || port > SHRT_MAX || argv[optind] == NULL || strlen(argv[optind]) == 0)
    {
        // announce usage
        printf("%s\n", usage);

        // return 2 just like bash's builtins
        return 2;
    }

    // start server
    start(port, argv[optind]); // starts the server configured to a specific port and assigns a root directory to it

    // listen for SIGINT (aka control-c)
    signal(SIGINT, handler);
    
    // Now the server is running, so from here on is doing the "serving" work
    // accept connections one at a time
    while (true)
    {
        // reset server's state
        reset();

        // wait until client is connected
        if (connected())
        {
            // parse client's HTTP request
            ssize_t octets = parse();
            if (octets == -1)
            {
                continue;
            }

            // extract request's request-line // extracts the line GET /cat.html HTTP/1.1
            // http://www.w3.org/Protocols/rfc2616/rfc2616-sec5.html
            const char* haystack = request;
            char* needle = strstr(haystack, "\r\n"); //\r\n is what separates the header and beginning of the needle
            
            if (needle == NULL)
            {
                error(400);
                continue;
            }
            else if (needle - haystack + 2 > LimitRequestLine) // needle - haystack + 2 -> size of request line
            {
                error(414);
                continue;
            }   
            char line[needle - haystack + 2 + 1]; // 2 for \r\n and 1 for NULL?
            strncpy(line, haystack, needle - haystack + 2); // copy haystack until \r\n, so copy the request line ...
            line[needle - haystack + 2] = '\0'; // apend a NULL caharacter in the end

            // log request-line
            printf("%s", line); //this prints (in white) GET /cat.html HTTP/1.1

            // TODO: validate request-line
            
            // method must be GET
            if (strncmp(line, "GET", 3) != 0) 
            {
                error(405);
                continue;
            }
            
            // request target must begin with "/"
            char* line_pt = strchr(line, ' '); // provar amb " /"
            
            char target_init[2] = "";
            strncpy(target_init, line_pt, 2);

            if (strncmp(target_init, " /",2) != 0)
            {   
                error(501);
                continue;
            }
            
            // request target must not contain "
            if (strchr(line, '"') != NULL)
            {   
                error(400);
                continue;
            }
            
            // version must be "HTTP/1.1"
            const char needle_1[9] = "HTTP/1.1\0";
            
            const char* line_ct = line;
            
            char* needle_1_pt = strcasestr(line_ct, needle_1);
            
            if(needle_1_pt == NULL)
            {
                error(505);
                continue;
            }            
            
            // absolute path must contain a "."
            // extract absolute path, but later
            // trim in order to tale needle out
            
            line_pt = strchr(line, '/'); 
            
            int ln_abs_path = needle_1_pt - line_pt; // lenght
            
            char abs_path[ln_abs_path]; // crec que és aquest l'ordre (needle_1_pt - line_pt)
                        
            memset(abs_path, 0, ln_abs_path); // initialize abs_path to 0
            
            strncpy(abs_path, line_pt, (ln_abs_path)); // beware buffer overruns /////
            
            abs_path[ln_abs_path - 1] = '\0'; // append null value, just to make sure
            
            // printf("%s\n", abs_path); // check before and after
            // printf("%d\n", ln_abs_path);// em dona 9
            
            if (strchr(abs_path, '.') == NULL) 
            {   
                error(501);
                continue;
            }

            // TODO: extract query from request-target // this is the stuff after a question mark
            
            char* query = malloc(1*sizeof(octet));
            *query = '\0';
            char* query_bg = strchr(abs_path, '?'); // beginning query
                                  
            if (query_bg != NULL)
            {
                char* query_end = strchr(abs_path, '\0');
                int query_ln = query_end - query_bg;
                
                if (query_ln > 1)
                {
                    query = realloc(query, query_ln);
                    memset(query, 0, query_ln);
                    strcpy(query, query_bg + 1); // si no funciona provar strncpy
                    query[query_ln - 1] = '\0'; // append NULL in the end
               
                    // printf("\033[36m"); // prints in blue
                    // printf("%s\n", query);
                    // printf("\033[39m\n");
                }
                abs_path[query_bg - abs_path] = '\0'; // takes the query out of the absolute path to ensure it exists
            }              
                
            // TODO: concatenate root and absolute-path
            char path[strlen(root) + ln_abs_path - 1];
            memset(path, 0, strlen(root) + ln_abs_path - 1);
            strcpy (path, root);
            strcat (path, abs_path);
            
            // printf("\033[36m"); // prints in blue
            // printf("%s\n", path);
            // printf("\033[39m\n");

            // TODO: ensure path exists
            
            if (access(path, F_OK) == -1)
            {
                error(404);
                continue;
            }
            // TODO: ensure path is readable
            if (access(path, R_OK) == -1)
            {
                error(403);
                continue;
            }
            // TODO: extract path's extension
            char* ext_pt_bg = strchr(path, '.');
            char* ext_pt_end = strchr(path, '\0');
            int ext_len = ext_pt_end - ext_pt_bg; // + 1 for the NULL character
            ////////////////// NOT 100% if the NULL character is copyed /////////////////////////

            
            char extension[ext_len];
            memset(extension, 0, ext_len);
            strcpy(extension, ext_pt_bg + 1); // strcpy already copies the terminating null 

            
            // printf("\033[36m"); // prints in blue
            // printf("%s\n", extension);
            // printf("\033[39m\n");
            
            // SO FAR SO GOOD

            // dynamic content
            if (strcasecmp("php", extension) == 0)
            {
                // open pipe to PHP interpreter
                char* format = "QUERY_STRING=\"%s\" REDIRECT_STATUS=200 SCRIPT_FILENAME=\"%s\" php-cgi";
                char command[strlen(format) + (strlen(path) - 2) + (strlen(query) - 2) + 1];
                sprintf(command, format, query, path);
                
                // free query, doesn't seem to be needed anymore
                free (query); 

                file = popen(command, "r"); // popen opens a pipe to a process php-cgi and returns a file pointer (file)
                if (file == NULL)
                {
                    error(500);
                    continue;
                }
                
                // load file // load will load file into the message body
                ssize_t size = load();
                if (size == -1)
                {
                    error(500);
                    continue;
                }

                // subtract php-cgi's headers from body's size to get content's length
                haystack = body; // body is the whole content that the client asked for
                
                needle = memmem(haystack, size, "\r\n\r\n", 4);
                if (needle == NULL)
                {
                    error(500);
                    continue;
                }
                size_t length = size - (needle - haystack + 4);

                // respond to client
                if (dprintf(cfd, "HTTP/1.1 200 OK\r\n") < 0) // prints first headers to cfd
                                                             // num of chars. If <0 it goes to the beginning of the while loop
                                                             // which restets cfd to -1.
                {
                    continue;
                }
                if (dprintf(cfd, "Connection: close\r\n") < 0)
                {
                    continue;
                }
                if (dprintf(cfd, "Content-Length: %i\r\n", length) < 0)
                {
                    continue;
                }
                if (write(cfd, body, size) == -1) // prints body to cfd
                {
                    continue;
                }
            }

            // static content
            else
            {
                // look up file's MIME type
                const char* type = lookup(extension);
                
                // printf("\033[36m"); // prints in blue
                // printf("%s\n", type);
                // printf("\033[39m\n");
                    
                if (type == NULL)
                {
                    error(501);
                    continue;
                }

                // open file
                file = fopen(path, "r"); // here is where the magic happens. Opens the file in the server
                if (file == NULL)
                {
                    error(500);
                    continue;
                }
                
                // printf("\033[36m"); // prints in blue
                // printf("%s\n", path);
                // printf("\033[39m\n");
                
                // load file
                ssize_t length = load(); // after this function the variable body carries all the file
                if (length == -1)        // left is only to extract the headers form body and copy the
                {                        // rest of it to cfd.       
                    error(500);
                    continue;
                }

                // TODO: respond to client // crec que és el mateix que a dynamic content
                                           // mirar tambe forensics
                                           
                // respond to client
                if (dprintf(cfd, "HTTP/1.1 200 OK\r\n") < 0) // prints first headers to cfd
                                                             // num of chars. If <0 it goes to the beginning of the while loop
                                                             // which restets cfd to -1.
                {
                    continue;
                }
                if (dprintf(cfd, "Connection: close\r\n") < 0)
                {
                    continue;
                }
                if (dprintf(cfd, "Content-Length: %i\r\n", length) < 0)
                {
                    continue;
                }
                if (dprintf(cfd, "Content-Type: %s\r\n\r\n", type) < 0)
                {
                    continue;
                }
                if (write(cfd, body, length) == -1) // prints body to cfd
                {
                    continue;
                }
            }
            
            // announce OK
            printf("\033[32m");
            printf("HTTP/1.1 200 OK");
            printf("\033[39m\n");
        }
    }
}

/**
 * Accepts a connection from a client, blocking (i.e., waiting) until one is heard.
 * Upon success, returns true; upon failure, returns false.
 */
bool connected(void)
{   
    // sockaddr is a structure that contains the the address family (http) and the socket address.
    struct sockaddr_in cli_addr; // declare ONE struct socket address (contains family and IP)
    memset(&cli_addr, 0, sizeof(cli_addr)); // memset fills the first sizeof(cli_addr) bytes with 0 (zeroes) to &cli_addr. IOW it initializes it to 0. 
    socklen_t cli_len = sizeof(cli_addr); // socklen_t is an unsigned 32 bites int. cli_len is the size of the client address
     
     /** 
     * accept does the work of establishing the connection. Opens a port for the client(sockaddr).
     * it takes the server socket sfd (it is the request the browser sends) to create a listening socket,
     * on success it returns the listening socket, or its descriptor (kind of pointer)
     * encara que diu "the new created socket is not in listening state"
     */
     
     cfd = accept(sfd, (struct sockaddr*) &cli_addr, &cli_len);

     
    
    if (cfd == -1)
    {
        return false;
    }
    return true;
}

/**
 * Handles client errors (4xx) and server errors (5xx).
 */
bool error(unsigned short code)
{
    // ensure client's socket is open
    if (cfd == -1)
    {
        return false;
    }

    // ensure code is within range
    if (code < 400 || code > 599)
    {
        return false;
    }

    // determine Status-Line's phrase
    // http://www.w3.org/Protocols/rfc2616/rfc2616-sec6.html#sec6.1
    const char* phrase = NULL;
    switch (code)
    {
        case 400: phrase = "Bad Request"; break;
        case 403: phrase = "Forbidden"; break;
        case 404: phrase = "Not Found"; break;
        case 405: phrase = "Method Not Allowed"; break;
        case 413: phrase = "Request Entity Too Large"; break;
        case 414: phrase = "Request-URI Too Long"; break;
        case 418: phrase = "I'm a teapot"; break;
        case 500: phrase = "Internal Server Error"; break;
        case 501: phrase = "Not Implemented"; break;
        case 505: phrase = "HTTP Version Not Supported"; break;
    }
    if (phrase == NULL)
    {
        return false;
    }

    // template
    char* template = "<html><head><title>%i %s</title></head><body><h1>%i %s</h1></body></html>";
    char content[strlen(template) + 2 * ((int) log10(code) + 1 - 2) + 2 * (strlen(phrase) - 2) + 1];
    int length = sprintf(content, template, code, phrase, code, phrase);

    // respond with Status-Line
    if (dprintf(cfd, "HTTP/1.1 %i %s\r\n", code, phrase) < 0)
    {
        return false;
    }

    // respond with Connection header
    if (dprintf(cfd, "Connection: close\r\n") < 0)
    {
        return false;
    }

    // respond with Content-Length header
    if (dprintf(cfd, "Content-Length: %i\r\n", length) < 0)
    {
        return false;
    }

    // respond with Content-Type header
    if (dprintf(cfd, "Content-Type: text/html\r\n") < 0)
    {
        return false;
    }

    // respond with CRLF
    if (dprintf(cfd, "\r\n") < 0)
    {
        return false;       // till here one sees the response headers. Appears only in Telnet.
    }
                            // and then the body, that might be 404 not found or whatsoever. Appears only in Telnet.
    // respond with message-body
    if (write(cfd, content, length) == -1)
    {
        return false;
    }

    // announce Response-Line // aquesta resposta apareix al servidor
    printf("\033[31m");
    printf("HTTP/1.1 %i %s", code, phrase); // en vermell, quan hi ha algún problema
    printf("\033[39m\n");

    return true;
}

/**
 * Loads file into message-body.
 */
ssize_t load(void)
{
    // ensure file is open
    if (file == NULL)
    {
        return -1;
    }

    // ensure body isn't already loaded
    if (body != NULL)
    {
        return -1;
    }

    // buffer for octets
    octet buffer[OCTETS];

    // read file
    ssize_t size = 0;
    while (true)
    {
        // try to read a buffer's worth of octets
        ssize_t octets = fread(buffer, sizeof(octet), OCTETS, file); // reads from file and puts it into buffer (its ptr)
                                                                     // reads 512 octets at the time
                                                                     // the number of blocks (of 512) returned is octets
        // check for error
        if (ferror(file) != 0)
        {
            if (body != NULL)
            {
                free(body);
                body = NULL;
            }
            return -1;
        }

        // if octets were read, append to body
        if (octets > 0)
        {
            body = realloc(body, size + octets); //resizing
            if (body == NULL)                    // will pass 512 octets (bytes)everyt time.
            {                                    // in the last block it will pass a value between 512 and 0
                return -1;                       // and next it will pass 0, so it goes out the loop
            }
            memcpy(body + size, buffer, octets); // appending
            size += octets;
        }

        // check for EOF
        if (feof(file) != 0)
        {
            break;
        }
    }
    return size;
}

/**
 * Handles signals.
 */
void handler(int signal)
{
    // control-c
    if (signal == SIGINT)
    {
        // ensure this isn't considered an error
        // (as might otherwise happen after a recent 404)
        errno = 0;

        // announce stop
        printf("\033[33m");
        printf("Stopping server\n");
        printf("\033[39m");

        // stop server
        stop();
    }
}

/**
 * Returns MIME type for supported extensions, else NULL.
 */
const char* lookup(const char* extension)
{
    // TODO
        
    if (strcasecmp("css", extension) == 0)
    {
        return "text/css";
    }
    
    if (strcasecmp("html", extension) == 0)
    {
        return "text/html";
    }
   
    if (strcasecmp("gif", extension) == 0)
    {
        return "inmage/gif";
    }
    
    if (strcasecmp("ico", extension) == 0)
    {
        return "image/x-icon";
    }
        
    if (strcasecmp("jpg", extension) == 0)
    {
        return "image/jpeg";
    }
   
    if (strcasecmp("js", extension) == 0)
    {
        return "text/javascript";
    }
    
    if (strcasecmp("png", extension) == 0)
    {
        return "image/png";
    }
    return NULL;
    /**
    switch (extension)
    {
        case "css": return text/css;
        case "html": return text/html;
        case "gif": return image/gif;
        case "ico": return image/x-icon;
        case "jpg": return image/jpeg;
        case "js": return text/javascript;
        case "png": return image/png;
    }
    */
}

/**
 * Parses an HTTP request. // it reads not from a file, but from a network connection
 */
ssize_t parse(void)
{
    // ensure client's socket is open
    if (cfd == -1)
    {
        return -1;
    }

    // ensure request isn't already parsed
    if (request != NULL)
    {
        return -1;
    }

    // buffer for octets
    octet buffer[OCTETS];

    // parse request 
    ssize_t length = 0;
    while (true)
    {
        // read from socket
        ssize_t octets = read(cfd, buffer, sizeof(octet) * OCTETS); // read from cfd and save it in the buffer
        if (octets == -1)
        {
            error(500);
            return -1;
        }

        // if octets have been read, remember new length
        if (octets > 0)
        {
            request = realloc(request, length + octets);  // chahges the size of request to  length + octets
            if (request == NULL)
            {
                return -1;
            }
            memcpy(request + length, buffer, octets); // append the corresponding bites to the request
            length += octets;
        }

        // else if nothing's been read, socket's been closed
        else
        {
            return -1;
        }
        
        // now we have the info we need, so we analyze it

        // search for CRLF CRLF
        int offset = (length - octets < 3) ? length - octets : 3;
        char* haystack = request + length - octets - offset;
        // search for the needle in the haystack
        char* needle = memmem(haystack, request + length - haystack, "\r\n\r\n", 4);
        
        if (needle != NULL)
        {
            // trim to one CRLF and null-terminate
            length = needle - request + 2 + 1;
            // cut the request so that it only contains the values we are interested in
            // which are the headers GET /cat.html HTTP/1.1
            request = realloc(request, length); 
            if (request == NULL)
            {
                return -1;
            }
            request[length - 1] = '\0';
            break;
        }

        // if buffer's full and we still haven't found CRLF CRLF,
        // then request is too large
        if (length - 1 >= LimitRequestLine + LimitRequestFields * LimitRequestFieldSize)
        {
            error(413);
            return -1;
        }
    }
    return length;
}

/**
 * Resets server's state, deallocating any resources.
 */
void reset(void)
{
    // free response's body
    if (body != NULL)
    {
        free(body);
        body = NULL;
    }

    // close file
    if (file != NULL)
    {
        fclose(file);
        file = NULL;
    }

    // free request
    if (request != NULL)
    {
        free(request);
        request = NULL;
    }

    // close client's socket
    if (cfd != -1)
    {
        close(cfd);
        cfd = -1;
    }
}

/**
 * Starts server.
 */
void start(short port, const char* path)
{
    // path to server's root
    root = realpath(path, NULL);
    if (root == NULL)
    {
        stop();
    }

    // ensure root exists
    if (access(root, F_OK) == -1)
    {
        stop();
    }

    // ensure root is executable
    if (access(root, X_OK) == -1)
    {
        stop();
    }

    // announce root
    printf("\033[33m"); // tells bash to change the text color to brown
    printf("Using %s for server's root", root);
    printf("\033[39m\n"); // tells bash to stop coloring

    // create a socket
    sfd = socket(AF_INET, SOCK_STREAM, 0); // Creates the server socket
    if (sfd == -1)
    {
        stop();
    }

    // allow reuse of address (to avoid "Address already in use")
    int optval = 1;
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    // assign name to socket
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr)); // initiaize ot (fill it with 0)
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port); // transform from host to network (little endian to big endian)
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY); // the same for the server address
    if (bind(sfd, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) == -1) // Assigning theaddress to the socket.
    {
        stop();
    }

    // listen for connections
    if (listen(sfd, SOMAXCONN) == -1)
    {
        stop();
    }

    // announce port in use
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    if (getsockname(sfd, (struct sockaddr*) &addr, &addrlen) == -1)
    {
        stop();
    }
    printf("\033[33m");
    printf("Listening on port %i", ntohs(addr.sin_port)); // which is the second printf line, sayng that it has started.
    printf("\033[39m\n");
}

/**
 * Stop server, deallocating any resources.
 */
void stop(void)
{
    // preserve errno across this function's library calls
    int errsv = errno;

    // reset server's state
    reset();

    // free root, which was allocated by realpath
    if (root != NULL)
    {
        free(root);
    }

    // close server socket
    if (sfd != -1)
    {
        close(sfd);
    }
    
    // terminate process
    if (errsv == 0)
    {
        // success
        exit(0);
    }
    else
    {
        // announce error
        printf("\033[33m");
        printf("%s", strerror(errsv));
        printf("\033[39m\n");

        // failure
        exit(1);
    }
}
