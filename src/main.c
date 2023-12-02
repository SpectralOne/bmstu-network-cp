#define _GNU_SOURCE

#include <arpa/inet.h>
#include <fcntl.h>
#include <magic.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "log.h"

#define INDEX_HTML_PATH "../public/index.html"
#define FAVICON_PATH "../public/favicon.ico"

#define MAX_THREADS 150
#define MAX_REQUEST_SIZE 4096
#define MAX_RESPONSE_SIZE 4096
#define MAX_BUFFER_SIZE 4096

#define RESPONSE_HEADERS \
  "HTTP/1.1 200 OK\r\nContent-Length: %lld\r\nContent-Type: %s\r\n\r\n"
#define FORBIDDEN "HTTP/1.1 403 Forbidden\r\n\r\n"
#define NOT_FOUND "HTTP/1.1 404 Not Found\r\n\r\n"
#define NOT_ALLOWED "HTTP/1.1 405 Method Not Allowed\r\n\r\n"

typedef struct {
  int client_sockfd;
  struct sockaddr_in client_addr;
} request_t;

int send_file_with_response(FILE* file, const char* path, char* response,
                            request_t* req, struct magic_set* magic) {
  log_trace("Processing file at path: %s", path);

  fseek(file, 0, SEEK_END);
  long long int file_size = ftell(file);
  fseek(file, 0, SEEK_SET);

  log_debug("File size = %lld", file_size);

  sprintf(response, RESPONSE_HEADERS,
          (long long int)strlen(response) + file_size, magic_file(magic, path));

  write(req->client_sockfd, response, strlen(response));

  log_trace("Sending file at path: %s", path);

  while (file_size > 0) {
    uint8_t buffer[MAX_BUFFER_SIZE] = {0};
    size_t bytes_readed = fread(buffer, 1, MAX_BUFFER_SIZE, file);
    file_size -= bytes_readed;
    write(req->client_sockfd, buffer, bytes_readed);
  }

  log_trace("File sent");

  return 0;
}

void request_handler(request_t* req) {
  log_trace("Thread found, started request handling activity");

  char request[MAX_REQUEST_SIZE] = {0};
  char response[MAX_RESPONSE_SIZE] = {0};
  char dir_buff[MAX_BUFFER_SIZE] = {0};

  struct magic_set* magic = magic_open(MAGIC_MIME | MAGIC_CHECK);
  magic_load(magic, NULL);

  read(req->client_sockfd, request, sizeof(request));

  char method[10] = {0};
  char path[256] = {0};
  sscanf(request, "%s %s", method, path);

  log_debug("Method = %s Path = %s", method, path);

  if (strcmp(method, "GET") != 0 && strcmp(method, "HEAD") != 0) {
    sprintf(response, NOT_ALLOWED);

    log_info("Method %s not allowed", method);

    write(req->client_sockfd, &response, strlen(response));
    close(req->client_sockfd);
    free(req);
    return;
  }

  if (strcmp(path, "/") == 0 && strcmp(method, "GET") == 0) {
    log_info("Returning default HTML");

    FILE* index_html = fopen(INDEX_HTML_PATH, "rb");

    send_file_with_response(index_html, INDEX_HTML_PATH, response, req, magic);

    fclose(index_html);

    log_info("/ request succeded");
    return;
  }

  if (strcmp(path, "/favicon.ico") == 0 && strcmp(method, "GET") == 0) {
    log_info("Returning favicon");

    FILE* icon = fopen(FAVICON_PATH, "rb");

    send_file_with_response(icon, FAVICON_PATH, response, req, magic);

    fclose(icon);

    log_info("/favicon.ico request succeded");
    return;
  }

  getcwd(dir_buff, MAX_BUFFER_SIZE);
  char full_path[MAX_BUFFER_SIZE] = {0};
  strcpy(full_path, dir_buff);
  strcat(full_path, path);

  log_debug("Full path = %s", full_path);

  if (strstr(full_path, "..") != NULL) {
    sprintf(response, FORBIDDEN);

    log_info("Path forbidden: %s", full_path);

    write(req->client_sockfd, &response, strlen(response));
    close(req->client_sockfd);
    free(req);
    return;
  }

  FILE* file = fopen(full_path, "rb");
  if (file == NULL) {
    sprintf(response, NOT_FOUND);

    log_info("File %s not found", full_path);

    write(req->client_sockfd, &response, strlen(response));
    close(req->client_sockfd);
    free(req);
    return;
  }

  if (strcmp(method, "GET") == 0) {
    send_file_with_response(file, full_path, response, req, magic);
  } else if (strcmp(method, "HEAD") == 0) {
    sprintf(response, RESPONSE_HEADERS, (long long int)strlen(response),
            magic_file(magic, full_path));
    write(req->client_sockfd, &response, strlen(response));

    log_info("HEAD request succeded");
  }

  close(req->client_sockfd);

  fclose(file);
  free(req);
}

int main(void) {
  setbuf(stdout, NULL);

  int server_sockfd;
  int client_sockfd;
  struct sockaddr_in server_addr;
  struct sockaddr_in client_addr;
  socklen_t client_addr_len;

  server_sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_sockfd < 0) {
    log_error("Failed to create socket");
    exit(EXIT_FAILURE);
  }

  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(8080);

  if (bind(server_sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) <
      0) {
    log_error("Failed to bind socket");
    exit(EXIT_FAILURE);
  }

  if (listen(server_sockfd, SOMAXCONN) < 0) {
    log_error("Failed to listen");
    exit(EXIT_FAILURE);
  }

  pthread_t threads[MAX_THREADS];
  for (int i = 0; i < MAX_THREADS; ++i) {
    threads[i] = 0;
  }

  log_info("Started at 0.0.0.0:8080");

  int ready = 0, max_fd = server_sockfd;
  fd_set rset, tset;
  FD_ZERO(&rset);
  FD_ZERO(&tset);
  FD_SET(server_sockfd, &rset);
  while (1) {
    memcpy(&tset, &rset, sizeof(rset));
    ready = pselect(max_fd + 1, &tset, NULL, NULL, NULL, NULL);
    if (ready == -1) {
      log_fatal("pselect die");
      continue;
    }

    if (!FD_ISSET(server_sockfd, &tset)) {
      continue;
    }

    client_addr_len = sizeof(client_addr);
    client_sockfd =
        accept(server_sockfd, (struct sockaddr*)&client_addr, &client_addr_len);
    if (client_sockfd < 0) {
      log_error("Failed to accept");
      continue;
    }

    request_t* req = (request_t*)malloc(sizeof(request_t));
    if (req == NULL) {
      log_fatal("Failed to allocate memmory for request");
      continue;
    }
    req->client_sockfd = client_sockfd;
    req->client_addr = client_addr;

    log_trace("Request accepted, started thread lookup");

    // FD_SET(client_sockfd, &rset);

    // if (max_fd < client_sockfd) max_fd = client_sockfd;

    for (int i = 0; i < MAX_THREADS; ++i) {
      if (threads[i] == 0 || (pthread_tryjoin_np(threads[i], NULL) == 0)) {
        pthread_create(&threads[i], NULL, (void* (*)(void*))request_handler,
                       (void*)req);
        break;
      }
    }
  }

  return EXIT_SUCCESS;
}
