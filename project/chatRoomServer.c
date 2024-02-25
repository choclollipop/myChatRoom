#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include "threadPool.h"
#include "balanceBinarySearchTree.h"
#include <strings.h>
#include <sqlite3.h>
#include <signal.h>
#include <string.h>
#include "chatRoomServer.h"
#include <json-c/json.h>

#define SERVER_PORT     8080
#define LISTEN_MAX      128

#define MIN_THREADS     10
#define MAX_THREADS     20
#define MAX_QUEUE_CAPACITY  10

#define BUFFER_SQL          100
#define FLUSH_BUFFER        10
#define DEFAULT_DATABASE    25
#define DEFAULT_CHAT        401
#define DEFAULT_LEN         40
#define BUFFER_SIZE         300
#define DIFF                5

/* 创建数据库句柄 */
sqlite3 * g_chatRoomDB = NULL;
/* 创建一个客户端存放好友信息的数据库 */
sqlite3 * g_clientMsgDB = NULL;

/* 通信套接字 */
int acceptfd = 0;

/* 主界面选择 */
enum CLIENT_CHOICE
{
    LOG_IN = 1,
    REGISTER,
    EXIT,
};

/* 运行状态码 */
enum STATUS_CODE
{
    ON_SUCCESS,
    ERROR = -1,
};

/* 聊天室功能选择 */
enum FUNC_CHOICE
{
    F_FRIEND_VIEW = 1,
    F_FRIEND_INCREASE,
    F_FRIEND_DELETE,
    F_PRIVATE_CHAT,
    F_CREATE_GROUP,
    F_INVITE_GROUP,
    F_GROUP_CHAT,
    F_EXIT,
};

/* 前置声明 */
/* 聊天室功能 */
int chatRoomFunc(chatRoom * chat, message * Msg);

/* AVL比较器：以登录名做比较 */
int compareFunc(void * val1, void * val2)
{
    clientNode * client = (clientNode *)val1;
    clientNode * data = (clientNode *)val2;

    int ret = strncmp(client->loginName, data->loginName, sizeof(client->loginName));

    if (ret > 0)
    {
        return 1;
    }
    else if (ret < 0)
    {
        return -1;
    }

    return 0;
}

/* AVL打印器 */
int printfFunc(void * val)
{
    clientNode * client = (clientNode *)val;
    printf("在线用户登录名：%s\n", client->loginName);

}

/* 客户端下线，关闭资源 */
int destoryClientSource()
{
    // close(acceptfd);
    sqlite3_close(g_clientMsgDB);

    return ON_SUCCESS;
}

/* 服务器的初始化 */
int chatRoomServerInit(int socketfd)
{
    struct sockaddr_in serverAddress;
    /* 存储服务器信息 */
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(SERVER_PORT);
    serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);

    socklen_t serverAddressLen = sizeof(serverAddress);

    /* 设置端口复用 */
    int enableOpt = 1;
    int ret = setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, (void *) &enableOpt, sizeof(enableOpt));
    if (ret == -1)
    {
        perror("setsockopt error");
    }

    /* 绑定服务器端口信息 */
    ret = bind(socketfd, (struct sockaddr *)&serverAddress, serverAddressLen);
    if (ret == -1)
    {
        perror("bind error");
    }
    /* 监听 */
    ret = listen(socketfd, LISTEN_MAX);
    if (ret == -1)
    {
        perror("listen error");
    }

}

#if 0
/* 捕捉信号关闭资源 */
void sigHander(int sig)
{

    printf("客户端下线\n");

    destoryClientSource();
    /* 进程结束 */
    pthread_exit(NULL);
}

#endif

/* 服务器登录 */
int chatRoomServerLoginIn(chatRoom * chat, message * Msg, char *** result, int * row, int * column, char ** errMsg)
{
    int acceptfd = chat->communicateFd;
    BalanceBinarySearchTree * onlineList = chat->online;

    ssize_t writeBytes = 0;

    /* 储存sql语句 */
    char sql[BUFFER_SQL];
    bzero(sql, sizeof(sql));

    int ret = 0;

    /* 判断用户输入是否正确 */
    sprintf(sql, "select * from user where password = '%s' and id = '%s'", Msg->clientLogInPasswd, Msg->clientLogInName);
    ret = sqlite3_get_table(g_chatRoomDB, sql, result, row, column, errMsg);
    if (ret != SQLITE_OK)
    {
        printf("select error : %s\n", *errMsg);
        return ERROR;
    }

    /* id和密码输入正确 */
    if (*row > 0)
    {
        clientNode client;
        bzero(&client, sizeof(client));
        bzero(client.loginName, sizeof(client.loginName));
        /* 将句柄和用户名封装成结点插入到在线列表中 */
        strncpy(client.loginName, Msg->clientLogInName, sizeof(Msg->clientLogInName));
        client.communicateFd = acceptfd;

        if (balanceBinarySearchTreeIsContainAppointVal(onlineList, (void *)&client))
        {
            bzero(Msg->message, sizeof(Msg->message));
            strncpy(Msg->message, "用户已在线", sizeof(Msg->message)); //防溢出

            /* 将提示信息发送给客户端 */
            writeBytes = write(acceptfd, Msg, sizeof(struct message));
            if (writeBytes < 0)
            {
                perror("write error");
                return ERROR;
            }
            
            return ON_SUCCESS;
        }

        /* 加锁 */
        pthread_mutex_lock(&g_mutex);

        /* 插入在线列表 */
        balanceBinarySearchTreeInsert(onlineList, (void *)&client);

        /* 解锁 */
        pthread_mutex_unlock(&g_mutex);

        bzero(Msg->message, sizeof(Msg->message));
        strncpy(Msg->message, "登录成功！", sizeof(Msg->message)); //防溢出

        /* 将提示信息发送给客户端 */
        writeBytes = write(acceptfd, Msg, sizeof(struct message));
        if (writeBytes < 0)
        {
            perror("write error");
            return ERROR;
        }

        /* 登录成功之后直接到功能界面 */
        ret = chatRoomFunc(chat, Msg);
        if (ret < 0)
        {
            printf("chatRoomFunc error\n");
            return ERROR;
        }

    }
    else
    {
        bzero(Msg->message, sizeof(Msg->message));
        strncpy(Msg->message, "用户名或密码输入错误，请重新输入", sizeof(Msg->message));
        writeBytes = write(acceptfd, Msg, sizeof(struct message));
        if (writeBytes < 0)
        {
            perror("write error");
            return ERROR;
        }
    }

    return ON_SUCCESS;
}

/* 服务器注册 */
int chatRoomServerRegister(chatRoom * chat, message * Msg, char *** result, int * row, int * column, char ** errMsg)
{
    int acceptfd = chat->communicateFd;
    BalanceBinarySearchTree * onlineList = chat->online;

    ssize_t writeBytes = 0;

    /* 储存sql语句 */
    char sql[BUFFER_SQL];
    bzero(sql, sizeof(sql));

    int ret = 0;

    /* 查询账号是否已经被注册 */
    sprintf(sql, "select id from user where id = '%s'", Msg->clientLogInName);
    ret = sqlite3_get_table(g_chatRoomDB, sql, result, row, column, errMsg);
    if (ret != SQLITE_OK)
    {
        printf("select error:%s\n", *errMsg);
        // close(acceptfd);
        // exit(-1);
    }

    if (*row > 0)
    {
        bzero(Msg->message, sizeof(Msg->message));
        strncpy(Msg->message, "登录名已存在，请重新输入!", sizeof(Msg->message));
        writeBytes = write(acceptfd, Msg, sizeof(struct message));
        if (writeBytes < 0)
        {
            perror("write error");
            return ERROR;
        }

    }
    else
    {
        /* 新用户，插入数据库中 */
        sprintf(sql, "insert into user values('%s', '%s')", Msg->clientLogInName, Msg->clientLogInPasswd);
        ret = sqlite3_exec(g_chatRoomDB, sql, NULL, NULL, errMsg);
        if (ret != SQLITE_OK)
        {
            printf("insert error: %s \n", *errMsg);
            // close(acceptfd);
            // pthread_exit(NULL);
        }

        clientNode client;
        bzero(&client, sizeof(client));
        bzero(client.loginName, sizeof(client.loginName));

        strncpy(client.loginName, Msg->clientLogInName, sizeof(Msg->clientLogInName));
        client.communicateFd = acceptfd;

        /* 加锁 */
        pthread_mutex_lock(&g_mutex);

        /* 插入在线列表 */
        balanceBinarySearchTreeInsert(onlineList, (void *)&client);

        /* 解锁 */
        pthread_mutex_unlock(&g_mutex);

        /* 打印在线列表 */
        balanceBinarySearchTreeLevelOrderTravel(onlineList);

        bzero(Msg->message, sizeof(Msg->message));
        strncpy(Msg->message, "注册成功！", sizeof(Msg->message));
        writeBytes = write(acceptfd, Msg, sizeof(struct message));
        if (writeBytes == -1)
        {
            printf("write error:%s\n", *errMsg);
            // close(acceptfd);
            // pthread_exit(NULL);
        }

        ret = chatRoomFunc(chat, Msg);
        if (ret < 0)
        {
            printf("chatRoomFunc error\n");
            return ERROR;
        }

    }

    return ON_SUCCESS;
}

/* 查看好友列表 */
int chatRoomServerSearchFriends(chatRoom * chat,  message * Msg, char *** result, int * row, int * column, char ** errMsg)
{
    int socketfd = chat->communicateFd;

    ssize_t readBytes = 0;
    ssize_t writeBytes = 0;

    /* 储存sql语句 */
    char sql[BUFFER_SQL];
    bzero(sql, sizeof(sql));

    sprintf(sql, "select friend from friends where id = '%s'", Msg->clientLogInName);
    int ret = 0;

    /* 好友列表json对象数组 */
    struct json_object * friendList = json_object_new_array();
    
    ret = sqlite3_get_table(g_clientMsgDB, sql, result, row, column, errMsg);
    if (ret != SQLITE_OK)
    {
        printf("get row from friend : %s\n", *errMsg);
        return ERROR;
    }

    if (*row < 0)
    {
        perror("get row error");
        return ERROR;
    }
    else
    {
        /* 传送好友 */    
        for (int idx = 0; idx < (*row) * (*column); idx += *column)
        {
            /* json数组添加内容 */
            json_object_array_add(friendList, json_object_new_string(*((*result + idx) + *column)));
        }
        struct json_object * jsonObj = json_object_new_object();

        /* 将数组和键加入到大的json对象中 */
        json_object_object_add(jsonObj, "id", friendList);

        /* 将最大的json对象转换成字符串 */
        const char *friendListVal = json_object_to_json_string(jsonObj);

        bzero(Msg->message, sizeof(Msg->message));
        Msg->func_choice = 0;
        Msg->choice = 0;
        Msg->func_choice = strlen(friendListVal);
        Msg->choice = *row;
        
        strncpy(Msg->message, "好友列表", sizeof(Msg->message));

        writeBytes = write(socketfd, Msg, sizeof(struct message));
        if (writeBytes < 0)
        {
            perror("write error");
        }

        writeBytes = write(socketfd, friendListVal, strlen(friendListVal));
        if (writeBytes < 0)
        {
            perror("write friendListVal error");
        }

    }

    return ON_SUCCESS;
}

/* 添加好友 */
int chatRoomAddFriends(chatRoom * chat, message * Msg, char *** result, int * row, int * column, char ** errMsg)
{
    int acceptfd = chat->communicateFd;
    /* 在线列表 */
    BalanceBinarySearchTree * onlineList = chat->online;

    ssize_t writeBytes = 0;
    
    /* 储存sql语句 */
    char sql[BUFFER_SQL];
    bzero(sql, sizeof(sql));

    sprintf(sql, "select * from user where id = '%s'", Msg->requestClientName);
    int ret = sqlite3_get_table(g_chatRoomDB, sql, result, row, column, errMsg);
    if (ret != SQLITE_OK)
    {
        printf("add friend select error : %s\n", *errMsg);
        return ERROR;
    }

    /* 该用户是否存在 */
    if (*row > 0)
    {
        clientNode client;
        bzero(&client, sizeof(client));
        bzero(client.loginName, sizeof(client.loginName));
        strncpy(client.loginName, Msg->requestClientName, sizeof(Msg->requestClientName));

        *result = NULL;
        *row = 0;
        *column = 0;
        sprintf(sql, "select * from friends where id = '%s' and friend = '%s'", Msg->clientLogInName, Msg->requestClientName);
        ret = sqlite3_get_table(g_clientMsgDB, sql, result, row, column, errMsg);
        if (ret != SQLITE_OK)
        {
            printf("select friend error:%s\n", *errMsg);
            return ERROR;
        }
        
        printf("row:%d\n", *row);
        if ((*row) > 0)
        {
            /* 有这个好友，不应该再添加 */
            bzero(Msg->message, sizeof(Msg->message));
            strncpy(Msg->message, "对方已经是您的好友", sizeof("对方已经是您的好友"));
            write(acceptfd, Msg, sizeof(struct message));
        }
        else
        {
            /* 没有此好友，但用户在线 */
            if (balanceBinarySearchTreeIsContainAppointVal(onlineList, (void *)&client))
            {
                sprintf(sql, "insert into friends values('%s', '%s', '')", Msg->clientLogInName, Msg->requestClientName);
                ret = sqlite3_exec(g_clientMsgDB, sql, NULL, NULL, errMsg);
                if (ret != SQLITE_OK)
                {
                    printf("insert error : %s\n", *errMsg);
                    return ERROR;
                }
                bzero(Msg->message, sizeof(Msg->message));
                strncpy(Msg->message, "对方同意了您的请求", sizeof("对方同意了您的请求"));
                write(acceptfd, Msg, sizeof(struct message));

            }
            else
            {
                bzero(Msg->message, sizeof(Msg->message));
                strncpy(Msg->message, "对方不在线，请稍后再试！", sizeof("对方不在线，请稍后再试！"));
                write(acceptfd, Msg, sizeof(struct message));
            }
        }

    }
    else
    {
        write(acceptfd, "对方不存在，请确认输入的id是否正确", sizeof("对方不存在，请确认输入的id是否正确"));
        //测试。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。
        printf("不存在\n");
    }


    return ON_SUCCESS;
    
}

/* 创建群聊 */
int chatRoomServerCreateGroupChat(chatRoom * chat, message * Msg , char *** result, int * row, int * column, char ** errMsg)
{
    printf("创建群聊\n");
    int acceptfd = chat->communicateFd;
    ssize_t writeBytes = 0;
    /* 储存sql语句 */
    char sql[BUFFER_SQL];
    bzero(sql, sizeof(sql));
    *result = NULL;
    *row = 0;
    *column = 0;

    /* 先判断我在不在里面 */
    sprintf(sql, "select * from groups where groupName = '%s' and id = '%s'", Msg->clientGroupName, Msg->clientLogInName);
    int ret = sqlite3_get_table(g_clientMsgDB, sql, result, row, column, errMsg);
    if (ret != SQLITE_OK)
    {
        printf("select * from groups: %s\n", (char *)errMsg);
        return 0;
    }

    printf("520 get groups row: %d\n", *row);

    if (*row < 0)
    {
        printf("get row error in groups");
        return ERROR;
    }
    else if (*row == 0)
    {
        /* 如果没有创建群聊--加入到表格 */
        sprintf(sql, "insert into groups values('%s', '%s', '')", Msg->clientGroupName, Msg->clientLogInName);
        int ret = sqlite3_exec(g_clientMsgDB, sql, NULL, NULL, errMsg);
        if (ret != SQLITE_OK)
        {
            printf("insert into groups:%s\n", *errMsg);
            return ERROR;
        }
        bzero(Msg->message, sizeof(Msg->message));
        strncpy(Msg->message, "创建群聊成功", sizeof(Msg->message));
        writeBytes = write(acceptfd, Msg, sizeof(struct message));
        if (writeBytes < 0)
        {
            perror("write error");
            return ERROR;
        } 
    }
    else if (*row > 0)
    {
        bzero(Msg->message, sizeof(Msg->message));
        strncpy(Msg->message, "该群聊名称已存在，请重新输入", sizeof(Msg->message));
        writeBytes = write(acceptfd, Msg, sizeof(struct message));
        if (writeBytes < 0)
        {
            perror("write error");
            return ERROR;
        }
    }

    printf("create groups test\n");

    return  ON_SUCCESS;
}

/* 邀请好友进群 */
int chatRoomServerAddPeopleInGroup(chatRoom * chat, message *Msg, char *** result, int * row, int * column, char ** errMsg)
{
    int acceptfd = chat->communicateFd;
    char sql[BUFFER_SQL];
    bzero(sql, sizeof(sql));

    /* 判断该用户是否存在在群中 */
    sprintf(sql, "select * from groups where groupName = '%s' and id = '%s'", Msg->clientGroupName, Msg->requestClientName);
    int ret = sqlite3_get_table(g_clientMsgDB, sql, result, row, column, errMsg);
    if (ret != SQLITE_OK)
    {
        printf("select * from groups: %s\n", (char *)errMsg);
        return 0;
    }

    if (*row > 0)
    {
        bzero(Msg->message, sizeof(Msg->message));
        strncpy(Msg->message, "该用户已存在", sizeof(Msg->message));
        write(acceptfd, Msg, sizeof(struct message));
        return ON_SUCCESS;
    }

    *row = 0;
    *column = 0;
    *result = NULL;
    sprintf(sql, "select * from groups where groupName = '%s'", Msg->clientGroupName);
    ret = sqlite3_get_table(g_clientMsgDB, sql, result, row, column, errMsg);
    if (ret != SQLITE_OK)
    {
        printf("select * from groups: %s\n", (char *)errMsg);
        return 0;
    }

    if (*row < 0)
    {
        printf("get row error in groups");
        return ERROR;
    }
    else if (*row > 0)
    {
        sprintf(sql, "insert into groups values('%s' , '%s', '');", Msg->clientGroupName, Msg->requestClientName);
        int ret = sqlite3_exec(g_clientMsgDB, sql, NULL, NULL, errMsg);
        if (ret != SQLITE_OK)
        {
            printf("create table error:%s\n", (char *)*errMsg);
            exit(-1);
        }

        bzero(Msg->message, sizeof(Msg->message));
        strncpy(Msg->message, "邀请成功!", sizeof(Msg->message));
        int writeBytes = write(acceptfd, Msg, sizeof(struct message));
        if (writeBytes < 0)
        {
            perror("write error");
            return ERROR;
        }

    }
    else if (*row == 0)
    {
        bzero(Msg->message, sizeof(Msg->message));
        strncpy(Msg->message, "该群名不存在，请重新输入!", sizeof(Msg->message));
        int writeBytes = write(acceptfd, Msg, sizeof(struct message));
        if (writeBytes < 0)
        {
            perror("write error");
            return ERROR;
        }

    }
    
    return ON_SUCCESS;
}

/* 发起群聊 */
int chatRoomStartCommunicate(chatRoom * chat, message *Msg, char *** result, int * row, int * column, char ** errMsg)
{
    int acceptfd = chat->communicateFd;
    ssize_t writeBytes = 0;
    ssize_t readBytes = 0;

    /* 储存sql语句 */
    char sql[BUFFER_SQL];
    bzero(sql, sizeof(sql));

    char copy[DEFAULT_CHAT];
    bzero(copy, sizeof(copy));

    /* 判断是否在群内 */
    sprintf(sql, "select id from groups where groupName = '%s' and id = '%s'", Msg->clientGroupName, Msg->clientLogInName);
    int ret = sqlite3_get_table(g_clientMsgDB, sql, result, row, column, errMsg);
    if (ret != SQLITE_OK)
    {
        printf("select id in group error: %s\n", *errMsg);
    }

    if (*row == 0)
    {
        strncpy(Msg->message, "没有该群/该群无成员", sizeof(Msg->message));
        writeBytes = write(acceptfd,  Msg, sizeof(struct message));
        if (writeBytes < 0)
        {
            perror("write start  error");
            return ERROR;
        }
        return ON_SUCCESS;
    }

    /* 根据群名获得群内所有的id */
    *row = 0;
    *column = 0;
    *result = NULL;
    sprintf(sql, "select id from groups where groupName = '%s' and id != '%s'", Msg->clientGroupName, Msg->clientLogInName);
    ret = sqlite3_get_table(g_clientMsgDB, sql, result, row, column, errMsg);
    if (ret != SQLITE_OK)
    {
        printf("select id in group error: %s\n", *errMsg);
    }

    if (*row < 0)
    {
        perror("get  id in group error");
        exit(-1);
    }
    else 
    {
        /* 我先进入到发送信息的线程中 */
        bzero(Msg->message, sizeof(struct message));
        strncpy(Msg->message, "发起群聊", sizeof(Msg->message));
        /* 发送提示信息 */
        write(acceptfd, Msg, sizeof(struct message));
        if (writeBytes < 0)
        {
            perror("write start  error");
            return ERROR;
        }

        /* 循环读 */
        while (1)
        {
            readBytes = read(acceptfd, Msg, sizeof(struct message));
            if (readBytes < 0)
            {
                printf("error read\n");
                return ERROR;
            }
            else if (readBytes == 0)
            {
                printf("客户端断开连接\n");
                return ERROR;
            }

            strncpy(copy, Msg->message, sizeof(copy));

            /* 读完我先要找到群成员id并发送 */
            /*  */
            for (int idx = 0; idx <=(*row); idx++) 
            {    
                /* 在线列表 */
                BalanceBinarySearchTree * onlineList = chat->online;

                AVLTreeNode * onlineNode = NULL;
            
                clientNode client;
                bzero(client.loginName, sizeof(client.loginName));
                strncpy(client.loginName, (const char *)(result[0])[idx], sizeof(client.loginName));//

                /* 查看群成员是否在线 */
                if (balanceBinarySearchTreeIsContainAppointVal(onlineList, (void *)&client))
                {
                    /* 该用户在线 */
                    AVLTreeNode * onlineNode = baseAppointValGetAVLTreeNode(onlineList, (void *)&client);
                    if (onlineNode == NULL)
                    {
                        perror("get node error");
                        return ERROR;
                    }

                    client = *(clientNode *)onlineNode->data;
                    /* 请求通信对象的通信句柄 */
                    int requestfd = client.communicateFd;

                    /* 找到一个套接字就发送 */
                    if (!strncmp(Msg->message, "q", sizeof(Msg->message)))
                    {
                        return ON_SUCCESS;
                    }
  
                    /* 修改发送的格式 */
                    char buffer[BUFFER_CHAT - DEFAULT_LOGIN_NAME - DEFAULT_GROUP_NAME - DIFF];
                    bzero(buffer, sizeof(buffer));
                    strncpy(buffer, copy, sizeof(buffer) - 1);
                
                    /* 存放新的内容 */
                    bzero(Msg->message, sizeof(Msg->message));
                    sprintf(Msg->message, "%s:\n [%s]:%s\n", Msg->clientGroupName, Msg->clientLogInName, buffer);

                    /* 给群成员发送信息 */
                    write(requestfd, Msg, sizeof(struct message));
        
                    }
            }
           
        }
    }

    return ON_SUCCESS;
}

/* 寻找目标用户套接字并发送消息 */
int chatRoomChatMessage(chatRoom * chat, message * Msg)
{
    /* 通信句柄 */
    int acceptfd = chat->communicateFd;
    /* 在线列表 */
    BalanceBinarySearchTree * onlineList = chat->online;

    AVLTreeNode * onlineNode = NULL;
    ssize_t readBytes = 0;

    clientNode client;
    bzero(client.loginName, sizeof(client.loginName));
    
    strncpy(client.loginName, Msg->requestClientName, sizeof(client.loginName));

    if (balanceBinarySearchTreeIsContainAppointVal(onlineList, (void *)&client))
    {
        /* 该用户在线 */
        AVLTreeNode * onlineNode = baseAppointValGetAVLTreeNode(onlineList, (void *)&client);
        if (onlineNode == NULL)
        {
            perror("get node error");
            return ERROR;
        }

        client = *(clientNode *)onlineNode->data;
        /* 请求通信对象的通信句柄 */
        int requestfd = client.communicateFd;
        
        bzero(Msg->message, sizeof(struct message));
        strncpy(Msg->message, "对方在线，可以输入要传送的消息", sizeof(Msg->message));

        write(acceptfd, Msg, sizeof(struct message));

        while (1)
        {
            readBytes = read(acceptfd, Msg, sizeof(struct message));
            if (readBytes < 0)
            {
                printf("error read\n");
                return ERROR;
            }

            if (!strncmp(Msg->message, "q", sizeof(Msg->message)))
            {
                bzero(Msg->message, sizeof(Msg->message));
                strncpy(Msg->message, "对方结束会话", sizeof("对方结束会话"));
                write(requestfd, Msg, sizeof(struct message));
                return ON_SUCCESS;
            }
            char buffer[BUFFER_CHAT - DEFAULT_LOGIN_NAME - DIFF];
            bzero(buffer, sizeof(buffer));

            strncpy(Msg->requestClientName, Msg->clientLogInName, sizeof(Msg->requestClientName));
            strncpy(Msg->clientLogInName, client.loginName, sizeof(Msg->clientLogInName));
            strncpy(buffer, Msg->message, sizeof(buffer) - 1);
            sprintf(Msg->message, "[%s]:%s", Msg->requestClientName, buffer);
            write(requestfd, Msg, sizeof(struct message));
        }    
    }
    else
    {
        /* 用户不在线 */
        bzero(Msg->message, sizeof(struct message));
        strncpy(Msg->message, "对方不在线，请稍后再试", sizeof(Msg->message));
        write(acceptfd, Msg, sizeof(struct message));
    }

    return ON_SUCCESS;
}

/* 删除好友 */
int chatRoomDeleteFriens(chatRoom * chat, message * Msg, char *** result, int * row, int * column, char ** errmsg)
{
    int acceptfd = chat->communicateFd;

    int ret =0;
    char sql[BUFFER_SQL];
    bzero(sql, sizeof(sql));

    sprintf(sql, "select friend = '%s' from friends where id = '%s'", Msg->requestClientName, Msg->clientLogInName);
    ret = sqlite3_get_table(g_clientMsgDB, sql, result, row, column, errmsg);
    if (ret != SQLITE_OK)
    {
        printf("select friends error in delete friend:%s\n", *errmsg);
        return ERROR;
    }

    if (*row > 0)
    {
        bzero(sql, sizeof(sql));
        sprintf(sql, "delete from friends where friend = '%s'", Msg->requestClientName);

        ret = sqlite3_exec(g_clientMsgDB, sql, NULL, NULL, errmsg);
        if (ret != SQLITE_OK)
        {
            printf("delete friends error in delete friend:%s\n", *errmsg);
            return ERROR;
        }

        bzero(Msg->message, sizeof(Msg->message));
        strncpy(Msg->message, "删除好友成功", sizeof("删除好友成功"));
        write(acceptfd, Msg, sizeof(struct message));
    }
    else
    {
        bzero(Msg->message, sizeof(Msg->message));
        strncpy(Msg->message, "未找到该好友，请检查是否输入正确", sizeof("未找到该好友，请检查是否输入正确"));
        write(acceptfd, Msg, sizeof(struct message));
    }
    
    return ON_SUCCESS;
}

/* 删除在线用户 */
int deleteOnlineClient(chatRoom * chat, message * Msg)
{
    BalanceBinarySearchTree * onlineList = chat->online;

    clientNode onlineClient;
    bzero(&onlineClient, sizeof(onlineClient));
    bzero(onlineClient.loginName, sizeof(onlineClient.loginName));
    strncpy(onlineClient.loginName, Msg->clientLogInName, sizeof(onlineClient.loginName));

    /* 加锁 */
    pthread_mutex_lock(&g_mutex);

    /* 删除在线列表中的用户 */
    int ret = balanceBinarySearchTreeDelete(onlineList, (void *)&onlineClient);
    if (ret != 0)
    {
        pthread_mutex_unlock(&g_mutex);
        return ERROR;
    }

    /* 解锁 */
    pthread_mutex_unlock(&g_mutex);

    if (onlineList->root != NULL)
    {
        printf("根节点没有释放空间\n");
    }

    return ON_SUCCESS;
}

/* 主功能 */
void * chatHander(void * arg)
{
    pthread_detach(pthread_self());
    chatRoom chat = *(chatRoom *)arg;
    int clientfd = chat.communicateFd;

    char ** result = NULL;
    int row = 0;
    int column = 0;
    char * errMsg = NULL;

    ssize_t readBytes = 0;
    int ret = 0;
    int flag = 0;

    message Msg;
    bzero(&Msg, sizeof(Msg));
    bzero(Msg.clientLogInName, sizeof(Msg.clientLogInName));
    bzero(Msg.clientLogInPasswd, sizeof(Msg.clientLogInPasswd));

    while (1)
    {
        readBytes = read(clientfd, &Msg, sizeof(struct message));
        if (readBytes < 0)
        {
            perror("read error");
            close(clientfd);
            break;
        }
        else if (readBytes == 0)
        {
            /* 客户端下线 */
            //测试。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。
            printf("客户端下线\n");

            deleteOnlineClient(&chat, &Msg);

            close(clientfd);
            break;
        }

        flag = 0;

        switch (Msg.choice)
        {
        /* 登录 */
        case LOG_IN:

            ret = chatRoomServerLoginIn(&chat, &Msg, &result, &row, &column, &errMsg);
            if (ret != ON_SUCCESS)
            {
                perror("LOGIN ERROR");
                close(clientfd);
                flag = 1;
            }
            break;

        /* 注册功能 */
        case REGISTER:
            ret = chatRoomServerRegister(&chat, &Msg, &result, &row, &column, &errMsg);
            if (ret != ON_SUCCESS)
            {
                perror("LOGIN ERROR");
                close(clientfd);
                flag = 1;
            }
            break;
        case EXIT:
            /* 退出程序 */
            {
                printf("客户端下线\n");
                close(clientfd);
                // pthread_exit(NULL);
                printf("关闭套接字，主界面退出部分\n");
                flag = 1;
                return NULL;
            }
            break;
        default:
            break;
        }

        if (flag)
        {
            printf("线程退出\n");
            break;
        }
    }
    pthread_exit(NULL);
}

/* 聊天室功能 */
int chatRoomFunc(chatRoom * chat, message * Msg)
{
    /* 通信句柄 */
    int acceptfd = chat->communicateFd;
    /* 在线列表 */
    BalanceBinarySearchTree * onlineList = chat->online;

    ssize_t readBytes = 0;

    /* 数据库相关参数 */
    char ** result = NULL;
    int row = 0;
    int column = 0;
    char * errMsg = NULL;
    char * sql = NULL;

    /* 创建数据库 -- 存放好友表，群聊表和验证消息表 */
    int ret = sqlite3_open("clientMsg.db", &g_clientMsgDB);
    if (ret != SQLITE_OK)
    {
        perror("sqlite open error");
        return ERROR;
    }

    /* 创建储存好友的表 */
    sql = "create table if not exists friends (id text, friend text, history text)";
    ret = sqlite3_exec(g_clientMsgDB, sql, NULL, NULL, &errMsg);
    if (ret != SQLITE_OK)
    {
        printf("create table error:%s\n", errMsg);
        return ERROR;
    }

    /* 创建群的表 */
    sql = "create table if not exists groups (groupName text not null, id text not null, history text)";
    ret = sqlite3_exec(g_clientMsgDB, sql, NULL, NULL, &errMsg);
    if (ret != SQLITE_OK)
    {
        printf("create table groups error:%s\n", errMsg);
        return ERROR;
    }
    
    while (1)
    {
        //测试...............................................................................................
        printf("功能界面\n");
        
        /* 读取客户端功能函数发过来的Msg，根据其中的func_choice跳转到相应的函数中 */
        readBytes = read(acceptfd, Msg, sizeof(struct message));
        if (readBytes < 0)
        {
            printf("main func error\n");
            return ERROR;
        }
        else if (readBytes == 0)
        {
            //测试。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。。
            printf("客户端下线\n");
            
            deleteOnlineClient(chat, Msg);
            sqlite3_close(g_clientMsgDB);
            close(acceptfd);
            pthread_exit(NULL);
        }

        switch (Msg->func_choice)
        {
        /* 查看好友 */
        case F_FRIEND_VIEW:
            int ret = chatRoomServerSearchFriends(chat, Msg, &result, &row, &column, &errMsg);
            if (ret != ON_SUCCESS)
            {
                printf("view friend error\n");
                break;
            }

            break;

        /* 添加好友 */
        case F_FRIEND_INCREASE:
            result = NULL;
            row = 0;
            column = 0;

            ret = chatRoomAddFriends(chat, Msg, &result, &row, &column, &errMsg);
            if (ret != ON_SUCCESS)
            {
                printf("add friend error\n");
                break;
            }

            break;

        /* 删除好友 */
        case F_FRIEND_DELETE:
            ret = chatRoomDeleteFriens(chat, Msg, &result, &row, &column, &errMsg);
            if (ret != ON_SUCCESS)
            {
                printf("delete friend error\n");
                break;
            }

            break;

        /* 私聊 */
        case F_PRIVATE_CHAT:

            ret = chatRoomChatMessage(chat, Msg);
            if (ret != ON_SUCCESS)
            {
                printf("private chat error\n");
                sqlite3_close(g_clientMsgDB);
                return ERROR;
            }

            break;

         /* 创建群聊 */
        case F_CREATE_GROUP:
            ret = chatRoomServerCreateGroupChat(chat, Msg , &result, &row, &column, &errMsg);
            if (ret != ON_SUCCESS)
            {
                printf("create group error\n");
                sqlite3_close(g_clientMsgDB);
                return ERROR;
            }
            break;


        /* 群聊拉人 */
        case F_INVITE_GROUP:
            ret = chatRoomServerAddPeopleInGroup(chat, Msg , &result, &row, &column, &errMsg);
            if (ret != ON_SUCCESS)
            {
                printf("invite friends in group error\n");
                sqlite3_close(g_clientMsgDB);
                return ERROR;
            }
            break;

        /* 发起群聊 */
        case F_GROUP_CHAT:
            ret = chatRoomStartCommunicate(chat, Msg , &result, &row, &column, &errMsg);
            if (ret != ON_SUCCESS)
            {
                printf("group chat error\n");
                sqlite3_close(g_clientMsgDB);
                return ERROR;
            }
            break;

        case F_EXIT:
            /* 退出 */
            printf("退出\n");

            deleteOnlineClient(chat, Msg);

            sqlite3_close(g_clientMsgDB);

            return ON_SUCCESS;
        
        default:
            break;
        }

    }

    return ON_SUCCESS;
}
 
int main()
{
    /* 初始化线程池 */
    // threadPool pool;

    // threadPoolInit(&pool, MIN_THREADS, MAX_THREADS, MAX_QUEUE_CAPACITY);

    /* 初始化锁：用来实现对在线列表的互斥访问 */
    pthread_mutex_init(&g_mutex, NULL);

    int socketfd = socket(AF_INET, SOCK_STREAM, 0);
    if (socketfd == -1)
    {
        perror("socket error");
        pthread_mutex_destroy(&g_mutex);
        // threadPoolDestroy(&pool);
        exit(-1);
    }
    chatRoomServerInit(socketfd);
    
    /* 打开数据库 */
    int ret = sqlite3_open("chatRoom.db", &g_chatRoomDB);
    if (ret != SQLITE_OK)
    {
        perror("sqlite open error");
        exit(-1);
    }

    /* 创建储存所有用户的表 */
    char * ermsg = NULL;
    const char * sql = "create table if not exists user (id text primary key not null, password text not null)";
    ret = sqlite3_exec(g_chatRoomDB, sql, NULL, NULL, &ermsg);
    if (ret != SQLITE_OK)
    {
        printf("create table error:%s\n", ermsg);
        sqlite3_close(g_chatRoomDB);
        pthread_mutex_destroy(&g_mutex);
        // threadPoolDestroy(&pool);
        exit(-1);
    }

    struct sockaddr_in clientAddress;
    socklen_t clientAddressLen = sizeof(clientAddress);


    /* 建立在线链表 */
    BalanceBinarySearchTree * onlineList;

    ret = balanceBinarySearchTreeInit(&onlineList, compareFunc, printfFunc);
    if (ret != 0)
    {
        perror("create online list error");
        close(socketfd);
        sqlite3_close(g_chatRoomDB);
        sqlite3_close(g_clientMsgDB);
        pthread_mutex_destroy(&g_mutex);
        // threadPoolDestroy(&pool);
        exit(-1);
    }

    /* 将句柄放到chat中保存 */
    chatRoom chat;
    bzero(&chat, sizeof(chat));
    chat.online = onlineList;

    /* 建立连接 */
    while (1)
    {
        acceptfd = accept(socketfd, (struct sockaddr *)&clientAddress, &clientAddressLen);
        if (acceptfd == -1)
        {
            perror("accept error");
            break;
        }

        chat.communicateFd = acceptfd;

        /* 向线程池中插入线程执行函数 */
        pthread_t tid;
        pthread_create(&tid, NULL, chatHander, (void *)&chat);
        // taskQueueInsert(&pool, chatHander, (void *)&chat);
    }

#if 0
    /* 捕捉信号 */
    /* ctr + c */
    signal(SIGINT, sigHander);
    /* ctr + z */
    signal(SIGTSTP, sigHander);

#endif

    /* 销毁资源 */
    close(socketfd);
    
    pthread_mutex_destroy(&g_mutex);
    balanceBinarySearchTreeDestroy(onlineList);
    sqlite3_close(g_chatRoomDB);
    sqlite3_close(g_clientMsgDB);
    // threadPoolDestroy(&pool);

    return 0;
}