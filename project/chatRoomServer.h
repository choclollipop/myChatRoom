#ifndef __CHAT_ROOM_SERVER_H_
#define __CHAT_ROOM_SERVER_H_
#include "common.h"
#include <pthread.h>


#define DEFAULT_LOGIN_NAME  20
#define DEFAULT_LOGIN_PAWD  16
#define BUFFER_SQL          100

typedef struct chatRoom
{
    BalanceBinarySearchTree * online;
    int communicateFd;
} chatRoom;

typedef struct clientNode
{
    char loginName[DEFAULT_LOGIN_NAME];
    char loginPawd[DEFAULT_LOGIN_PAWD];
    int communicateFd;
} clientNode;



/* 锁 */
pthread_mutex_t g_mutex;


/* 服务器的初始化,设置端口复用 */
int chatRoomServerInit(int * socketfd, int port);

/* 服务器登录 */
int chatRoomServerLoginIn(clientNode *client, chatRoom * chat);

/* 服务器注册 */
int chatRoomServerRegister(int socketfd, clientNode *client, BalanceBinarySearchTree * onlineList);

/* 查看好友列表 */
int chatRoomServerSearchFriends(int socketfd);

/* 添加好友 */
int chatRoomAddFriends(int socketfd, BalanceBinarySearchTree * onlineList);

#endif