#ifndef __CHAT_ROOM_CLIENT_H_
#define __CHAT_ROOM_CLIENT_H_


#define DEFAULT_LOGIN_NAME  21
#define DEFAULT_LOGIN_PAWD  17 


typedef struct clientNode
{
    char loginName[DEFAULT_LOGIN_NAME];
    char loginPawd[DEFAULT_LOGIN_PAWD];
} clientNode;


/* 客户端的功能主要实现 */
//int chatRoomFunc(int socketfd, const clientNode* client);

/* 客户端的初始化 */
int chatRoomClientInit(int socketfd);

/* 客户端的登录 */
int chatRoomClientLoginIn(int socketfd, clientNode *client);

/* 客户端的注册 */
int chatRoomClientRegister(int socketfd, clientNode *client);

/* 群聊 */
int chatRoomClientGroupChat(int socketfd, clientNode *client, BalanceBinarySearchTree * friendTree);

/* 拉人进群 */
int chatRoomClientAddPeopleInGroup();



#endif