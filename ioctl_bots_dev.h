#ifndef IOCTL_BOTS_DEV_H
#define IOCTL_BOTS_DEV_H

// Maximum length of message  buffer
#define BUFF_LEN 		64

// Structure to share data from process to device and vice-versa
typedef struct ProcessInfo {
    int id;
    char msg[BUFF_LEN];
} ProcessInfo;

// IOCTL Commands
#define COMMAND_JOIN    1
#define COMMAND_LEAVE   2
#define COMMAND_WRITE   3
#define COMMAND_READ    4

#define JOIN_CHATROOM       _IOWR('a', COMMAND_JOIN, ProcessInfo *)
#define LEAVE_CHATROOM      _IOW('a', COMMAND_LEAVE, ProcessInfo *)
#define WR_MESSAGE          _IOW('a', COMMAND_WRITE, ProcessInfo *)
#define RD_MESSAGE          _IOWR('a', COMMAND_READ, ProcessInfo *)

#endif