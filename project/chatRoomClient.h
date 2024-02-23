#ifndef __CHAT_ROOM_CLIENT_H_
#define __CHAT_ROOM_CLIENT_H_
#include "common.h"
#include "balanceBinarySearchTree.h"


#define DEFAULT_LOGIN_NAME  21
#define DEFAULT_LOGIN_PAWD  17
#define DEFAULT_GROUP_NAME  21

#define BUFFER_CHAT         401


typedef struct chatRoom
{
    int socketfd;
    BalanceBinarySearchTree * friend;
} chatRoom;

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


/* 客户端的功能主要实现 */
//int chatRoomFunc(int socketfd, const clientNode* client);

/* 客户端的初始化 */
int chatRoomClientInit(int socketfd);

/* 客户端的登录注册 */
int chatRoomClientLoginInRegister(int socketfd, message * Msg);

/* 删除好友 */
int chatRoomDeleteFriends(int socketfd, BalanceBinarySearchTree * friendTree, message * Msg);

/* 从服务器读取好友列表 */
int readFriends(int socketfd, message * Msg);

/* 打印好友列表 */
int printFrientList(int socketfd, BalanceBinarySearchTree * friendTree, message * Msg, int capacity);

/* 私聊发送信息 */
int chatRoomPrivateChat(message * Msg, int socketfd);

/* 私聊发送信息线程 */
void * sendMsg(void * arg);

/* 创建群聊 */
int chatRoomClientCreateGroupChat(int socketfd, message * Msg);

/* 邀请好友进群 */
int chatRoomClientAddPeopleInGroup(int socketfd, message * Msg, BalanceBinarySearchTree * friendTree);

/* 发起群聊 */
int chatRoomClientStartGroupCommunicate(int socketfd, message * Msg);

/* 群聊发送信息线程 */
void * sendGroupMsg(void * arg);

/* 聊天室功能 */
int chatRoomFunc(int socketfd, message * Msg);

/* 添加好友 */
int chatRoomClientAddFriends(int socketfd,  BalanceBinarySearchTree * friendTree, message * Msg);

#endif