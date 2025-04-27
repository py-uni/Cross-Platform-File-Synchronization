#include <stdio.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <limits.h>

#define EVENT_SIZE (sizeof(struct inotify_event))
#define LEN (5 * (EVENT_SIZE + 16))
#define SERVER_IP "127.0.0.1" // 替换为实际服务器IP
#define SERVER_PORT 8080

int fd, wd, sockfd;
struct sockaddr_in ser_addr;

void connect_to_server()
{
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        perror("socket");
        _exit(-1);
    }

    bzero(&ser_addr, sizeof(ser_addr));
    ser_addr.sin_family = AF_INET;
    ser_addr.sin_port = htons(SERVER_PORT);
    ser_addr.sin_addr.s_addr = inet_addr(SERVER_IP);

    int ret = connect(sockfd, (struct sockaddr *)&ser_addr, sizeof(ser_addr));
    if (ret != 0)
    {
        perror("connect");
        close(sockfd);
        _exit(-1);
    }
}

ssize_t send_all(int sockfd, const void *buf, size_t len)
{
    size_t total_sent = 0;
    char ack;
    while (total_sent < len)
    {
        ssize_t sent = send(sockfd, (char *)buf + total_sent, len - total_sent, 0);
        if (sent <= 0)
            return sent;

        // if (recv(sockfd, &ack, 1, 0) != 1 || ack != 'A')
        // {
        //     fprintf(stderr, "服务器确认失败\n");
        //     sent = 0;
        // }

        total_sent += sent;
    }
    return total_sent;
}

void send_file(int sockfds, const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp)
    {
        perror("fopen");
        close(sockfds);
        _exit(-1);
    }

    // 发送文件名（含长度前缀）
    uint32_t name_len = strlen(path);
    uint32_t net_name_len = htonl(name_len);
    send_all(sockfds, &net_name_len, sizeof(net_name_len));
    send_all(sockfds, path, name_len);

    // 发送文件内容
    unsigned char buf[4096];
    size_t bytes_read;
    while ((bytes_read = fread(buf, 1, sizeof(buf), fp)) > 0)
    {
        if (send_all(sockfds, buf, bytes_read) <= 0)
        {
            break;
        }
    }
    fclose(fp);
}

void *send_thread(void *arg)
{
    char *path = (char *)arg;
    int sockfds = socket(AF_INET, SOCK_STREAM, 0);
    int ret = connect(sockfds, (struct sockaddr *)&ser_addr, sizeof(ser_addr));

    if (ret != 0)
    {
        perror("connect");
        close(sockfds);
        free(path);
        return NULL;
    }
    send_file(sockfds, path);
    free(path);
    close(sockfds);
    return NULL;
}

void inotify_file()
{
    connect_to_server();
    char buffer[LEN];

    fd = inotify_init();
    if (fd < 0)
    {
        perror("inotify_init");
        _exit(-1);
    }

    // 监控文件关闭写入事件
    wd = inotify_add_watch(fd, "data", IN_CLOSE_WRITE);
    if (wd < 0)
    {
        perror("inotify_add_watch");
        close(fd);
        _exit(-1);
    }

    while (1)
    {
        ssize_t length = read(fd, buffer, LEN);
        if (length < 0)
        {
            perror("read");
            break;
        }

        for (char *ptr = buffer; ptr < buffer + length;)
        {
            struct inotify_event *event = (struct inotify_event *)ptr;
            if (event->mask & IN_CLOSE_WRITE)
            {
                pthread_t tid;
                char path[PATH_MAX];
                snprintf(path, sizeof(path), "data/%s", event->name);
                printf("正在同步文件: %s\n", event->name);
                char *path_copy = strdup(path);
                pthread_create(&tid, NULL, send_thread, path_copy);
                pthread_detach(tid);
            }
            ptr += EVENT_SIZE + event->len;
        }
    }
}

void signal_handler(int sig)
{
    inotify_rm_watch(fd, wd);
    close(fd);
    printf("\n退出\n");
    exit(0);
}

int main()
{
    signal(SIGINT, signal_handler);
    inotify_file();
    return 0;
}
