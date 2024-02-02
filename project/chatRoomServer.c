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
#define BUFFER_CHAT         450
#define DEFAULT_LEN         40
#define BUFFER_SIZE         300

/* 创建数据库句柄 */
sqlite3 * g_chatRoomDB = NULL;
/* 创建一个客户端存放好友信息的数据库 */
sqlite3 * g_clientMsgDB = NULL;

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
    F_GROUP_CHAT,
    F_EXIT,
};

/* 前置声明 */
/* 聊天室功能 */
int chatRoomFunc(chatRoom * chat, clientNode * client);

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
        close(socketfd);
        sqlite3_close(g_chatRoomDB);
        sqlite3_close(g_clientMsgDB);
        pthread_mutex_destroy(&g_mutex);
        // threadPoolDestroy(&pool);
        exit(-1);
    }

    /* 绑定服务器端口信息 */
    ret = bind(socketfd, (struct sockaddr *)&serverAddress, serverAddressLen);
    if (ret == -1)
    {
        perror("bind error");
        close(socketfd);
        sqlite3_close(g_chatRoomDB);
        sqlite3_close(g_clientMsgDB);
        pthread_mutex_destroy(&g_mutex);
        // threadPoolDestroy(&pool);
        exit(-1);
    }

    ret = listen(socketfd, LISTEN_MAX);
    if (ret == -1)
    {
        perror("listen error");
        close(socketfd);
        sqlite3_close(g_chatRoomDB);
        sqlite3_close(g_clientMsgDB);
        pthread_mutex_destroy(&g_mutex);
        // threadPoolDestroy(&pool);
        exit(-1);
    }

}


/* 读取客户端传输的登录名和密码 */
void readName(int acceptfd, struct clientNode * client)
{
    ssize_t readBytes = read(acceptfd, client->loginName, sizeof(client->loginName));
    if (readBytes < 0)
    {
        perror("read error");
        close(acceptfd);
        pthread_exit(NULL);
    }

    printf("登录名：%s\n", client->loginName);

    /* 清空缓冲区 */
    char flushBuffer[FLUSH_BUFFER];
    bzero(flushBuffer, sizeof(flushBuffer));
    read(acceptfd, flushBuffer, sizeof(flushBuffer));
}

/* 读取密码*/
void readPasswd(int acceptfd, struct clientNode * client)
{
    ssize_t readBytes = read(acceptfd, client->loginPawd, sizeof(client->loginPawd));
    if (readBytes < 0)
    {
        perror("read error");
        close(acceptfd);
        sqlite3_close(g_chatRoomDB);
        pthread_exit(NULL);
    }

    printf("登录密码：%s\n", client->loginPawd);
}

#if 0
/* 捕捉信号关闭资源 */
void sigHander(int sig)
{

    // close();
    // close(socketfd);
    
    // pthread_mutex_destroy(&g_mutex);
    // balanceBinarySearchTreeDestroy(chat.online);
    // sqlite3_close(chat.ppDb);
    // threadPoolDestroy(&pool);

    /* 进程结束 */
    exit(1);
}

#endif

/* 服务器登录 */
int chatRoomServerLoginIn(chatRoom * chat, clientNode *client)
{
    int acceptfd = chat->communicateFd;
    BalanceBinarySearchTree * onlineList = chat->online;
    /* 存储查询结果 */
    char ** result = NULL;
    int row = 0;
    int column = 0;
    int choice = 0;

    ssize_t readBytes = 0;
    ssize_t writeBytes = 0;
    char * errMsg = NULL;

    /* 储存sql语句 */
    char sql[BUFFER_SQL];
    bzero(sql, sizeof(sql));

    int ret = 0;
    int flag = 0;
    do {
        readName(acceptfd, client);
        readPasswd(acceptfd, client);

        result = NULL;
        row = 0;
        column = 0;

        /* 判断用户输入是否正确 */
        sprintf(sql, "select id = '%s' from user where password = '%s'", client->loginName, client->loginPawd);
        ret = sqlite3_get_table(g_chatRoomDB, sql, &result, &row, &column, &errMsg);
        if (ret != SQLITE_OK)
        {
            printf("select error : %s\n", errMsg);
            return ERROR;
        }

        /* id和密码输入正确 */
        if (row > 0)
        {
            /* 加锁 */
            pthread_mutex_lock(&g_mutex);

            /* 插入在线列表 */
            balanceBinarySearchTreeInsert(onlineList, &client);

            /* 解锁 */
            pthread_mutex_unlock(&g_mutex);

            writeBytes = write(acceptfd, "登录成功！\n", sizeof("登录成功！\n"));
            if (writeBytes < 0)
            {
                perror("write error");
                return ERROR;
            }

            ret = chatRoomFunc(chat, client);
            if (ret < 0)
            {
                printf("chatRoomFunc error\n");
                return ERROR;
            }

            flag = 0;
        }
        else
        {
            writeBytes = write(acceptfd, "用户名或密码输入错误，请重新输入\n", sizeof("用户名或密码输入错误，请重新输入\n"));
            if (writeBytes < 0)
            {
                perror("write error");
                return ERROR;
            }
            flag = 1;
            continue;
        }

        /* 打印在线列表 */
        balanceBinarySearchTreeLevelOrderTravel(onlineList);

        choice = 0;

    }while (flag);
    return ON_SUCCESS;
}

/* 服务器注册 */
int chatRoomServerRegister(chatRoom * chat, clientNode *client)
{
    int acceptfd = chat->communicateFd;
    BalanceBinarySearchTree * onlineList = chat->online;
    /* 存储查询结果 */
    char ** result = NULL;
    int row = 0;
    int column = 0;
    int choice = 0;

    ssize_t readBytes = 0;
    ssize_t writeBytes = 0;
    char * errMsg = NULL;

    /* 储存sql语句 */
    char sql[BUFFER_SQL];
    bzero(sql, sizeof(sql));

    int ret = 0;
    int flag = 0;
    do 
    {
        readName(acceptfd, client);

        row = 0;
        column = 0;
        result = NULL;
        sprintf(sql, "select id from user where id = '%s'", client->loginName);
        printf("登录名：%s", client->loginName);
        ret = sqlite3_get_table(g_chatRoomDB, sql, &result, &row, &column, &errMsg);
        if (ret != SQLITE_OK)
        {
            printf("select error:%s\n", errMsg);
            close(acceptfd);
            exit(-1);
        }

        if (row > 0)
        {
            writeBytes = write(acceptfd, "登录名已存在，请重新输入!\n", sizeof("登录名已存在，请重新输入!\n"));
            if (writeBytes < 0)
            {
                perror("write error");
                close(acceptfd);
                exit(-1);
            }
            flag = 1;
            continue;
        }
        else
        {
            writeBytes = write(acceptfd, "请输入登录密码：\n", sizeof("请输入登录密码：\n"));
            if (writeBytes < 0)
            {
                perror("write error");
                close(acceptfd);
                exit(-1);
            }

            readPasswd(acceptfd, client);

            /* 插入数据库中 */
            sprintf(sql, "insert into user values('%s', '%s')", client->loginName, client->loginPawd);
            ret = sqlite3_exec(g_chatRoomDB, sql, NULL, NULL, &errMsg);
            if (ret != SQLITE_OK)
            {
                printf("insert error: %s \n", errMsg);
                close(acceptfd);
                pthread_exit(NULL);
            }

            writeBytes = write(acceptfd, "注册成功！", sizeof("注册成功！"));
            if (writeBytes == -1)
            {
                printf("write error:%s\n", errMsg);
                close(acceptfd);
                pthread_exit(NULL);
            }

            /* 加锁 */
            pthread_mutex_lock(&g_mutex);

            /* 插入在线列表 */
            balanceBinarySearchTreeInsert(onlineList, &client);

            /* 解锁 */
            pthread_mutex_unlock(&g_mutex);

            /* 打印在线列表 */
            balanceBinarySearchTreeLevelOrderTravel(onlineList);

            ret = chatRoomFunc(chat, client);
            if (ret < 0)
            {
                printf("chatRoomFunc error\n");
                return ERROR;
            }

            choice = 0;

            flag = 0;
        }
    } while (flag);

    return ON_SUCCESS;
}

/* 查看好友列表 */
int chatRoomServerSearchFriends(chatRoom * chat)
{
    int socketfd = chat->communicateFd;
    /* 存储查询结果 */
    char ** result = NULL;
    int row = 0;
    int column = 0;
    int choice = 0;

    ssize_t readBytes = 0;
    ssize_t writeBytes = 0;
    char * errMsg = NULL;

    /* 储存sql语句 */
    char sql[BUFFER_SQL];
    bzero(sql, sizeof(sql));

    printf("查看好友功能\n");
    sprintf(sql, "select * from friend");
    int ret = 0;

    /* 好友列表json对象 */
    struct json_object * friendList = json_object_new_array();
    
    ret = sqlite3_get_table(g_clientMsgDB, sql, &result, &row, &column, &errMsg);
    if (ret != SQLITE_OK)
    {
        printf("select error : %s\n", errMsg);
        close(socketfd);
        return ERROR;
    }
    writeBytes = write(socketfd, &row, sizeof(int) * row);
    if (writeBytes < 0)
    {
        perror("write error");
        close(socketfd);
        sqlite3_close(g_clientMsgDB);
    }
    if (row <= 0)
    {
        perror("get row error");
        exit(-1);
    }
    else 
    {
        /* 传送好友 */    
        for (int idx = 1; idx <= row; idx++)
        {
            json_object_object_add(friendList, "id", json_object_new_string(result[idx]));
        }
        const char *friendListVal = json_object_to_json_string(friendList);
        writeBytes = write(socketfd, friendListVal, strlen(friendListVal));
        if (writeBytes < 0)
        {
            perror("write error");
            close(socketfd);
            sqlite3_close(g_clientMsgDB);
        }
        printf("id: %s\n",friendListVal);
    }

}

/* 添加好友 */
int chatRoomAddFriends(chatRoom * chat)
{

    int acceptfd = chat->communicateFd;
    BalanceBinarySearchTree * onlineList = chat->online;
    /* 存储查询结果 */
    char ** result = NULL;
    int row = 0;
    int column = 0;

    ssize_t readBytes = 0;
    ssize_t writeBytes = 0;
    char * errMsg = NULL;

    clientNode * requestClient = NULL;
    bzero(requestClient, sizeof(requestClient));
    bzero(requestClient, sizeof(requestClient->loginName));
    bzero(requestClient, sizeof(requestClient->loginPawd));
    
    /* 储存sql语句 */
    char sql[BUFFER_SQL];
    bzero(sql, sizeof(sql));
    int flag = 0;
    do
    {
        //测试
        printf("添加好友功能\n");
        readBytes = read(acceptfd, requestClient->loginName, sizeof(requestClient->loginName));
        if (readBytes < 0)
        {
            perror("read error");
        close(acceptfd);
        return ERROR;
    }
    if(!strncmp(requestClient->loginName, "q", sizeof(requestClient->loginName)))
    {
        return ON_SUCCESS;
    }

        sprintf(sql, "select id from user where id = '%s'", requestClient->loginName);
        sqlite3_get_table(g_chatRoomDB, sql, &result, &row, &column, &errMsg);
        if (row > 0)
        {
            write(acceptfd, "对方同意了您的请求", sizeof("对方同意了您的请求"));

        if (balanceBinarySearchTreeIsContainAppointVal(onlineList, requestClient))
            sprintf(sql, "inster into friend values(id = '%s')", requestClient->loginName);
            sqlite3_exec(g_clientMsgDB, sql, NULL, NULL, &errMsg);
            flag = 0;
        }
        else
        {
            AVLTreeNode * onlineNode = baseAppointValGetAVLTreeNode(onlineList, requestClient);
            int request = 1;
            write(acceptfd, &request, sizeof(request));
            write(acceptfd, "对方不存在，请确认输入的id是否正确", sizeof("对方不存在，请确认输入的id是否正确"));
            flag = 1;
        }

        /* 不在线 */
        int request = 0;
        write(acceptfd, &request, sizeof(request));

    } while (flag);

    return ON_SUCCESS;
    
}

/* 拉人进群 */
int chatRoomServerAddPeopleInGroup(const char *groupName, char *idBuffer)
{
    char sql[BUFFER_SQL];
    bzero(sql, sizeof(sql));
    char * errMsg = NULL;

    sprintf(sql, "insert into groups values('%s' , '%s')", groupName, idBuffer);
    int ret = sqlite3_exec(g_clientMsgDB, sql, NULL, NULL, &errMsg);
    if (ret != SQLITE_OK)
    {
        printf("create table error:%s\n", errMsg);
        exit(-1);
    }

    return ON_SUCCESS;
}

/* 创建群聊 */
int chatRoomServerGroupChat(chatRoom * chat)
{
    int acceptfd = chat->communicateFd;
    int flag = 0;

    char nameBuffer[DEFAULT_LOGIN_NAME];
    bzero(nameBuffer, sizeof(nameBuffer));

    char idBuffer[DEFAULT_LOGIN_NAME];
    bzero(idBuffer, sizeof(idBuffer));

    /* 读到群聊名称 */
    read(acceptfd, nameBuffer, sizeof(nameBuffer));
    struct json_object * group = json_tokener_parse(nameBuffer);
    struct json_object * groupName = json_object_object_get(group, "groupName");
    struct json_object * id = json_object_object_get(group, "id");

    const char * groupNameVal = json_object_to_json_string(groupName);
    const char * idVal = json_object_to_json_string(id);

    /* 储存sql语句 */
    char sql[BUFFER_SQL];
    bzero(sql, sizeof(sql));
    /* 存储查询结果 */
    char ** result = NULL;
    int row = 0;
    int column = 0;

    char * errMsg = NULL;

    /* 创建群的表 */
    sprintf(sql, "create table if not exists groups (groupName text not null, id text not null, history text)");
    int ret = sqlite3_exec(g_clientMsgDB, sql, NULL, NULL, &errMsg);
    if (ret != SQLITE_OK)
    {
        printf("create table error:%s\n", errMsg);
        sqlite3_close(g_clientMsgDB);
        exit(-1);
    }
    /* 先判断我在不在里面 */
    sprintf(sql, "select * from groups where groupName = '%s' and id = '%s'", groupNameVal, idVal);
    ret = sqlite3_get_table(g_clientMsgDB, sql, &result, &row, &column, &errMsg);
    if (ret != SQLITE_OK)
    {
        printf("create table error:%s\n", errMsg);
        sqlite3_close(g_clientMsgDB);
        exit(-1);
    }

    /* 如果没有创建群聊--加入到表格 */
    if (row < 0)
    {
        perror("get row error");
        sqlite3_close(g_clientMsgDB);
        exit(-1);
        json_object_put(group);
    }
    else if (row == 0)
    {
        sprintf(sql, "insert into groups values('%s', '%s')", groupNameVal, idVal);
        int ret = sqlite3_exec(g_clientMsgDB, sql, NULL, NULL, &errMsg);
        if (ret != SQLITE_OK)
        {
            printf("create table error:%s\n", errMsg);
            exit(-1);
        }
    }
    flag = 1; //创建完成，添加群聊成员

    /* 发送邀请好友进群 */
    write(acceptfd, &flag, sizeof(int));

    bzero(idBuffer, sizeof(idBuffer));

    read(acceptfd, idBuffer, sizeof(idBuffer));
    chatRoomServerAddPeopleInGroup(groupNameVal , idBuffer);
    
    write(acceptfd, "q", sizeof("q"));

    json_object_put(group);

    return  ON_SUCCESS;

}

/* 发起群聊 */
int chatRoomStartCommunicate(chatRoom * chat, clientNode *requestClient)
{
    int acceptfd = chat->communicateFd;

    ssize_t readBytes = 0;
    ssize_t writeBytes = 0;

    char readBuffer[BUFFER_SIZE];
    bzero(readBuffer, sizeof(readBuffer));

    int flag = 0;

    readBytes = read(acceptfd, readBuffer, sizeof(readBuffer));
    if (readBytes < 0)
    {
        perror("read error");
        exit(-1);
    }
    else if (readBytes == 0)
    {
        printf("客户端已经下线");
        exit(-1);
    }
    else
    {
        struct json_object * groupHistory = json_tokener_parse(readBuffer);
        struct json_object * groupName = json_object_object_get(groupHistory, "groupName");
        struct json_object * id = json_object_object_get(groupHistory, "id");
        struct json_object * history = json_object_object_get(groupHistory, "historyVal");

        const char * groupNameVal = json_object_to_json_string(groupName);
        const char * idVal = json_object_to_json_string(id);
        const char * historyVal = json_object_to_json_string(history);

        /* 根据群名获得所有的id */
        /* 储存sql语句 */
        char sql[BUFFER_SQL];
        bzero(sql, sizeof(sql));
        /* 存储查询结果 */
        char ** result = NULL;
        int row = 0;
        int column = 0;

        char * errMsg = NULL;

        sprintf(sql, "select id from groups where groupName = '%s'", groupNameVal);
        int ret = sqlite3_get_table(g_clientMsgDB, sql, &result, &row, &column, &errMsg);
        if (ret != SQLITE_OK)
        {
            printf("create table error:%s\n", errMsg);
            sqlite3_close(g_clientMsgDB);
            exit(-1);
        }

        if (row < 0)
        {
            perror("get row error");
            sqlite3_close(g_clientMsgDB);
            exit(-1);
        }
        else if (row == 0)
        {
            write(acceptfd, &flag, sizeof(int));
            sqlite3_close(g_clientMsgDB);
            exit(-1);
        }
        else
        {
            while (1)
            {
                for (int idx  = 0; idx <= row; idx++)
                {
                    for (int jdx = 0; jdx < column; jdx++)
                    {
                        if (strcmp(requestClient->loginName, result[idx * row + jdx]) == 0)
                        
                            acceptfd =  requestClient->communicateFd;
                        }
                        /* to do.. 私聊接口 */
                    }

                }

            }

        }

    }

/* 寻找目标用户套接字并发送消息 */
int chatRoomChatMessage(chatRoom * chat, clientNode * client, clientNode * requestClient)
{
    /* 通信句柄 */
    int acceptfd = client->communicateFd;
    /* 在线列表 */
    BalanceBinarySearchTree * onlineList = chat->online;
    AVLTreeNode * onlineNode = NULL;

    if ((onlineNode = baseAppointValGetAVLTreeNode(onlineList, (void *)&requestClient)) != NULL)
    {
        requestClient = (clientNode *)onlineNode->data;

        /* 请求通信对象的通信句柄 */
        int requestfd = requestClient->communicateFd;
        
        char chatBuffer[DEFAULT_CHAT];
        char chatWriteBuffer[BUFFER_CHAT];

        write(acceptfd, "对方在线，可以输入要传送的消息", sizeof("对方在线，可以输入要传送的消息"));

        while (1)
        {
            bzero(chatBuffer,sizeof(chatBuffer));
            bzero(chatWriteBuffer, sizeof(chatWriteBuffer));
            read(acceptfd, chatBuffer, sizeof(chatBuffer));
            if (!strncmp(chatBuffer, "q", sizeof(chatBuffer)))
            {
                write(requestfd, "对方结束会话", sizeof("对方结束会话"));
                return ON_SUCCESS;
            }

            sprintf(chatWriteBuffer, "[%s]:%s", client->loginName, chatBuffer);
            write(requestfd, chatWriteBuffer, sizeof(chatWriteBuffer));
        }
        
        return ON_SUCCESS;
    }
    else
    {
        write(acceptfd, "对方不在线，请给他留言(输出q退出)", sizeof("对方不在线，请给他留言(输出q退出)"));
    }

    return ON_SUCCESS;
}


/* 删除好友 */
int chatRoomDeleteFriens(chatRoom * chat, char *** result, int * row, int * column, char * errmsg)
{
    int acceptfd = chat->communicateFd;
    BalanceBinarySearchTree * onlineList = chat->online;
    clientNode delieteClient;
    bzero(&delieteClient, sizeof(delieteClient));
    bzero(delieteClient.loginPawd, sizeof(delieteClient.loginPawd));

    char sql[BUFFER_SQL];
    bzero(sql, sizeof(sql));
    sprintf(sql, "select * from friend where id = '%s'", delieteClient.loginName);
    int flag = 0;
    int ret = 0;
    do
    {
        bzero(delieteClient.loginName, sizeof(delieteClient.loginName));
        write(acceptfd, delieteClient.loginName, sizeof(delieteClient.loginName));
        if (!strncmp(delieteClient.loginName, "q", sizeof(delieteClient.loginName)))
        {
            /* 退出 */
            return ON_SUCCESS;
        }

        ret = sqlite3_get_table(g_clientMsgDB, sql, result, row, column, &errmsg);
        if (ret != SQLITE_OK)
        {
            printf("select friends error:%s\n", errmsg);
            return ERROR;
        }

        if (row > 0)
        {
            bzero(sql, sizeof(sql));
            sprintf(sql, "delete from friend where id = '%s'", delieteClient.loginName);

            ret = sqlite3_exec(g_clientMsgDB, sql, NULL, NULL, &errmsg);
            if (ret != SQLITE_OK)
            {
                printf("delete friends error:%s\n", errmsg);
                return ERROR;
            }

            flag = 0;
        }
        else
        {
            write(acceptfd, "未找到该好友，请检查是否输入正确", sizeof("未找到该好友，请检查是否输入正确"));
            flag = 1;
        }

    } while (flag);
    
    return ON_SUCCESS;
}

/* 聊天室功能 */
int chatRoomFunc(chatRoom * chat, clientNode * client)
{
    /* 通信句柄 */
    int acceptfd = client->communicateFd;
    /* 在线列表 */
    BalanceBinarySearchTree * onlineList = chat->online;

    ssize_t readBytes = 0;
    ssize_t writeBytes = 0;

    char nameBuffer[DEFAULT_LOGIN_NAME];
    bzero(nameBuffer, sizeof(nameBuffer));

    char sql[BUFFER_SQL];
    bzero(sql, sizeof(sql));

    /* 数据库相关参数 */
    char ** result = NULL;
    int row = 0;
    int column = 0;
    char * errMsg = NULL;

    /* 打开数据库 */
    char ptr[DEFAULT_DATABASE];
    bzero(ptr, sizeof(ptr));
    sprintf(ptr, "%s.db", client->loginName);

    int ret = sqlite3_open(ptr, &g_clientMsgDB);
    if (ret != SQLITE_OK)
    {
        perror("sqlite open error");
        close(acceptfd);
        return ERROR;
    }
    /* 创建储存好友的表 */
    /* 存储数据库错误信息 */
    strncpy(sql, "create table if not exists friend (id text primary key not null)", sizeof("create table if not exists friend (id text primary key not null)"));
    ret = sqlite3_exec(g_clientMsgDB, sql, NULL, NULL, &errMsg);
    if (ret != SQLITE_OK)
    {
        printf("create table error:%s\n", errMsg);
        sqlite3_close(g_clientMsgDB);
        close(acceptfd);
        return ERROR;
    }

    /* 创建请求对象 */
    clientNode requestClient;
    bzero(&requestClient, sizeof(requestClient));
    bzero(&requestClient, sizeof(requestClient.loginName));
    bzero(&requestClient, sizeof(requestClient.loginPawd));

    printf("创建数据库\n");

    int func_choice = 0;

    //测试
    printf("功能界面\n");
    while (1)
    {
        /* ⭐读取功能 */
        readBytes = read(acceptfd, &func_choice, sizeof(func_choice));
        if (readBytes < 0)
        {
            perror("read error");
            close(acceptfd);
            return ERROR;
        }

        switch (func_choice)
        {
        /* 查看好友 */
        case F_FRIEND_VIEW:
            chatRoomServerSearchFriends(chat);
            func_choice = 0;
            break;

        /* 添加好友 */
        case F_FRIEND_INCREASE:
            chatRoomAddFriends(chat);
            func_choice = 0;
            break;

        /* 删除好友 */
        case F_FRIEND_DELETE:
            ret = chatRoomDeleteFriens(chat, &result, &row, &column, errMsg);
            if (ret != ON_SUCCESS)
            {
                sqlite3_close(g_clientMsgDB);
                close(acceptfd);
                return ERROR;
            }

            func_choice = 0;
            break;

        case F_PRIVATE_CHAT:
            int len = 0;
            read(acceptfd, &len, sizeof(len));

            /* 打印在线列表 */
            balanceBinarySearchTreeInOrderTravel(onlineList);

            char * ptrObj = (char *)malloc(sizeof(char) * (len + 1));
            if (ptrObj == NULL)
            {
                perror("malloc error");
                close(acceptfd);
                sqlite3_close(g_clientMsgDB);
                return ERROR;
            }
            bzero(ptrObj, sizeof(ptrObj));        

            /* 读取对象 */
            read(acceptfd, ptrObj, len + 1);
            /* 将字符串转换成json对象 */
            struct json_object * requestObj = json_tokener_parse(ptrObj);
            /* 取用户名 */
            const char * requestPtr = json_object_get_string(json_object_object_get(requestObj, client->loginName));
            printf("requestPtr: %s\n", requestPtr);
            printf("requestPtr:%ld\n", strlen(requestPtr));

            strncpy(requestClient.loginName, requestPtr, sizeof(requestClient.loginName));
            printf("requestClient.loginNama1:%s\n", requestClient.loginName);

            /* 关闭json对象 */
            json_object_put(requestObj);

            if (balanceBinarySearchTreeIsContainAppointVal(onlineList, (void *)&requestClient))
            {
                AVLTreeNode * onlineNode = baseAppointValGetAVLTreeNode(onlineList, (void *)&requestClient);
                if (onlineNode == NULL)
                {
                    perror("get node error");
                    close(acceptfd);
                    sqlite3_close(g_clientMsgDB);
                    return ERROR;
                }

                requestClient = *(clientNode *)onlineNode->data;
                printf("requestClient.loginName : %s\n", requestClient.loginName);
                printf("requestClient.communicateFd : %d\n", requestClient.communicateFd);
                printf("acceptfd : %d\n", acceptfd);
                /* 请求通信对象的通信句柄 */
                int requestfd = requestClient.communicateFd;
                
                char chatBuffer[DEFAULT_CHAT];
                char chatWriteBuffer[BUFFER_CHAT];

                write(acceptfd, "对方在线，可以输入要传送的消息", sizeof("对方在线，可以输入要传送的消息"));

                while (1)
                {
                    bzero(chatBuffer,sizeof(chatBuffer));
                    bzero(chatWriteBuffer, sizeof(chatWriteBuffer));
                    read(acceptfd, chatBuffer, sizeof(chatBuffer));
                    if (!strncmp(chatBuffer, "q", sizeof(chatBuffer)))
                    {
                        write(requestfd, "对方结束会话", sizeof("对方结束会话"));
                        break;
                    }

                    sprintf(chatWriteBuffer, "[%s]:%s", client->loginName, chatBuffer);
                    write(requestfd, chatWriteBuffer, sizeof(chatWriteBuffer));
                }
                
                
            }
            else
            {
                write(acceptfd, "对方不在线，请给他留言(输出q退出)", sizeof("对方不在线，请给他留言(输出q退出)"));
            }    

            if (ptrObj)
            {
                free(ptrObj);
                ptrObj = NULL;
            }

            break;

         /* 创建群聊 */
        case F_CREATE_GROUP:
            chatRoomServerGroupChat(chat);
            func_choice = 0;
            break;

        /* 发起群聊 */
        case F_GROUP_CHAT:
            chatRoomStartCommunicate(chat, &requestClient);
            break;

        /* 退出 */
        case F_EXIT:

            break;
        
        default:
            break;
        }

    }
}

/* 主功能 */
void * chatHander(void * arg)
{
    /* 线程分离 */
    pthread_detach(pthread_self());
    /* 接收传递过来的结构体 */
    chatRoom * chat = (chatRoom *)arg;

    /* 通信句柄 */
    int acceptfd = chat->communicateFd;
    /* 在线列表 */
    BalanceBinarySearchTree * onlineList = chat->online;
    /* 数据库句柄 */
    // sqlite3 * g_chatRoomDB = chat->ppDb;

    int choice = 0;
    int ret = 0;

    /* 新建用户结点 */
    clientNode client;
    bzero(&client, sizeof(clientNode));
    bzero(client.loginName, sizeof(DEFAULT_LOGIN_NAME));
    bzero(client.loginPawd, sizeof(DEFAULT_LOGIN_PAWD));

    client.communicateFd = acceptfd;

    /* 程序运行 */
    while (1)
    {
        /* 循环接收选择的功能 */
        ssize_t readChoice = read(acceptfd, &choice, sizeof(choice));
        if (readChoice < 0)
        {
            perror("read error");
            close(acceptfd);
            pthread_exit(NULL);
        }

        switch (choice)
        {
        /* 登录功能 */
        case LOG_IN:
            ret = chatRoomServerLoginIn(chat, &client);
            if (ret != ON_SUCCESS)
            {
                perror("LOGIN ERROR");
                exit(-1);
            }

            break;
        
        /* 注册功能 */
        case REGISTER:

            chatRoomServerRegister(chat, &client);

            break;

        default:
            break;
        }

        /* 退出程序 */
        if (choice == EXIT)
        { 
            close(acceptfd);
            pthread_exit(NULL);
        }

    }

    pthread_exit(NULL);
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

    /* ⭐打开数据库 */
    ret = sqlite3_open("clientMsg.db", &g_clientMsgDB);
    if (ret != SQLITE_OK)
    {
        perror("sqlite open error");
        sqlite3_close(g_chatRoomDB);
        pthread_mutex_destroy(&g_mutex);
        exit(-1);
    }
    /* ⭐创建储存好友的表 */
    /* 存储数据库错误信息 */
    char * errMsg = NULL;
    sql = "create table if not exists friend (id text primary key not null)";
    ret = sqlite3_exec(g_clientMsgDB, sql, NULL, NULL, &errMsg);
    if (ret != SQLITE_OK)
    {
        printf("create table error:%s\n", errMsg);
        sqlite3_close(g_clientMsgDB);
        sqlite3_close(g_chatRoomDB);
        pthread_mutex_destroy(&g_mutex);
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

    chatRoom chat;
    bzero(&chat, sizeof(chat));

    chat.online = onlineList;
    
    int acceptfd = 0;
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
    close(acceptfd);
    close(socketfd);
    
    pthread_mutex_destroy(&g_mutex);
    balanceBinarySearchTreeDestroy(onlineList);
    sqlite3_close(g_chatRoomDB);
    sqlite3_close(g_clientMsgDB);
    // threadPoolDestroy(&pool);

    return 0;
}