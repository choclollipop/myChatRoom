#ifndef __CHAT_ROOM_SERVER_H_
#define __CHAT_ROOM_SERVER_H_
#include "common.h"
#include <pthread.h>


#define DEFAULT_LOGIN_NAME  20
#define DEFAULT_LOGIN_PAWD  16

typedef struct chatRoom
{
    BalanceBinarySearchTree * online;
    int communicateFd;
    pthread_mutex_t mutex;
    // sqlite3 * ppDb;
} chatRoom;

typedef struct clientNode
{
    char loginName[DEFAULT_LOGIN_NAME];
    char loginPawd[DEFAULT_LOGIN_PAWD];
    int communicateFd;
} clientNode;


/* 锁 */
pthread_mutex_t g_mutex;


/* 服务器的初始化 */
int chatRoomServerInit(int socketfd);

void * chatHander(void * arg);

/* 服务器登录 */
int chatRoomServerLoginIn(int socketfd, clientNode *client, BalanceBinarySearchTree * onlineList);

/* 服务器注册 */
int chatRoomServerRegister(int socketfd, clientNode *client, BalanceBinarySearchTree * onlineList);

int chatRoomServerSearchFriends();

#endif