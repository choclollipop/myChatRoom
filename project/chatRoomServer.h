#ifndef __CHAT_ROOM_SERVER_H_
#define __CHAT_ROOM_SERVER_H_
#include "common.h"
#include <pthread.h>


#define DEFAULT_LOGIN_NAME  21
#define DEFAULT_LOGIN_PAWD  17
#define DEFAULT_GROUP_NAME  21

#define BUFFER_CHAT         401

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
    int communicateFd;
} clientNode;

typedef struct message
{
    /* 主界面选择 */
    int choice;
    /* 功能界面选择 */
    int func_choice;
    /* 客户内容 */
    char clientLogInName[DEFAULT_LOGIN_NAME];
    char clientLogInPasswd[DEFAULT_LOGIN_PAWD];
    /* 群聊名称 */
    char clientGroupName[DEFAULT_GROUP_NAME];
    /* 消息发送对象 */
    char requestClientName[DEFAULT_LOGIN_NAME];
    /* 消息内容 */
    char message[BUFFER_CHAT];
} message;


/* 锁 */
pthread_mutex_t g_mutex;


/* 服务器的初始化 */
int chatRoomServerInit(int socketfd);

void * chatHander(void * arg);

/* 服务器登录 */
int chatRoomServerLoginIn(chatRoom * chat, message * Msg, char *** result, int * row, int * column, char ** errMsg);

/* 服务器注册 */
int chatRoomServerRegister(chatRoom * chat, message * Msg, char *** result, int * row, int * column, char ** errMsg);

/* 查看好友列表 */
int chatRoomServerSearchFriends(chatRoom * chat,  message * Msg, char *** result, int * row, int * column, char ** errMsg);

/* 添加好友 */
int chatRoomAddFriends(chatRoom * chat, message * Msg, char *** result, int * row, int * cloumn, char ** errMsg);

/* 删除好友 */
int chatRoomDeleteFriens(chatRoom * chat, message * Msg, char *** result, int * row, int * column, char * errmsg);

/* 寻找目标用户套接字并发送消息 */
int chatRoomChatMessage(chatRoom * chat, message * Msg);

/* 创建群聊 */
int chatRoomServerCreateGroupChat(chatRoom * chat, message * Msg , char *** result, int * row, int * column, char ** errMsg);

/* 邀请好友进群 */
int chatRoomServerAddPeopleInGroup(chatRoom * chat, message *Msg, char ** errMsg);

int chatRoomStartCommunicate();

#endif