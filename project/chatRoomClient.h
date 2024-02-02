#ifndef __CHAT_ROOM_CLIENT_H_
#define __CHAT_ROOM_CLIENT_H_
#include "common.h"
#include "balanceBinarySearchTree.h"


#define DEFAULT_LOGIN_NAME  21
#define DEFAULT_LOGIN_PAWD  17
#define BUFFER_CHAT         401


typedef struct clientNode
{
    char loginName[DEFAULT_LOGIN_NAME];
    char loginPawd[DEFAULT_LOGIN_PAWD];
} clientNode;

typedef struct message
{
    /* 主界面选择 */
    int choice;
    /* 功能界面选择 */
    int func_choice;
    /* 客户内容 */
    clientLogInName[DEFAULT_LOGIN_NAME];
    clientLogInPasswd[DEFAULT_LOGIN_PAWD];
    /* 消息内容 */
    char message[BUFFER_CHAT];
} message;


/* 客户端的功能主要实现 */
//int chatRoomFunc(int socketfd, const clientNode* client);

/* 客户端的初始化 */
int chatRoomClientInit(int socketfd);

/* 客户端的登录注册 */
int chatRoomClientLoginInRegister(int socketfd, message * Msg);

/* 删除好友 */
int chatRoomDeleteFriends(int socketfd, BalanceBinarySearchTree * friendTree);

/* 从服务器读取好友列表 */
int readFriends(int socketfd, BalanceBinarySearchTree * friendTree);

/* 群聊 */
int chatRoomClientGroupChat(int socketfd, clientNode *client, BalanceBinarySearchTree * friendTree);

/* 拉人进群 */
int chatRoomClientAddPeopleInGroup(int socketfd, clientNode *client, BalanceBinarySearchTree * friendTree);

/* 发起群聊 */
int chatRoomClientStartGroupCommunicate(int socketfd, clientNode *client, BalanceBinarySearchTree * friendTree);


#endif