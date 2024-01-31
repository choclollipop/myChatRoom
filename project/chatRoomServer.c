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

#define SERVER_PORT     8080
#define LISTEN_MAX      128

#define MIN_THREADS     10
#define MAX_THREADS     20
#define MAX_QUEUE_CAPACITY  10

#define DEFAULT_LOGIN_NAME  20
#define DEFAULT_LOGIN_PAWD  16
#define BUFFER_SQL          100
#define FLUSH_BUFFER        10
#define DEFAULT_DATABASE    25

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
    F_GROUP_CHAT,
};

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

/* AVL比较器：以登录名做比较 */
int compareFunc(void * val1, void * val2)
{
    clientNode * client = (clientNode *)val1;
    clientNode * data = (clientNode *)val2;

    if (client->loginName > data->loginName)
    {
        return 1;
    }
    else if (client->loginName < data->loginName)
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


/* 聊天室功能 */
int chatRoomFunc(chatRoom * chat, clientNode * client)
{
    /* 通信句柄 */
    int acceptfd = chat->communicateFd;
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

    printf("创建数据库\n");

    int func_choice = 0;

    //测试
    printf("功能界面\n");
    while (1)
    {
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
            printf("查看好友功能\n");

            sprintf(sql, "select * from friend");
            ret = sqlite3_get_table(g_clientMsgDB, sql, &result, &row, &column, &errMsg);
            if (ret != SQLITE_OK)
            {
                printf("select error : %s\n", errMsg);
                close(acceptfd);
                return ERROR;
            }
            if (row == 0)
            {
                writeBytes = write(acceptfd, "当前没有好友！\n", sizeof("当前没有好友！\n"));
                if (writeBytes < 0)
                {
                    perror("write error");
                    close(acceptfd);
                    sqlite3_close(g_clientMsgDB);
                }
            }
            else
            {
                /* 传送好友 */    
                for (int idx = 1; idx <= row; idx++)
                {
                    for (int jdx = 0; jdx < column; jdx++)
                    {
                        writeBytes = write(acceptfd, result[(idx * column) + jdx], sizeof(client->loginName));
                        if (writeBytes < 0)
                        {
                            perror("write error");
                            close(acceptfd);
                            sqlite3_close(g_clientMsgDB);
                        }
                        printf("id: %s\n", result[(idx * column) + jdx]);
                    }
                }
            }
            func_choice = 0;

            break;

        /* 添加好友 */
        case F_FRIEND_INCREASE:
#if 0
            //测试
            printf("添加好友功能\n");

            readBytes = read(acceptfd, nameBuffer, sizeof(nameBuffer));
            if (readBytes < 0)
            {
                perror("read error");
                close(acceptfd);
                return ERROR;
            }

            clientNode * requestClient;
            bzero(requestClient, sizeof(requestClient));
            bzero(requestClient, sizeof(requestClient->loginName));
            bzero(requestClient, sizeof(requestClient->loginPawd));

            if (balanceBinarySearchTreeIsContainAppointVal(onlineList, requestClient))
            {
                AVLTreeNode * onlineNode = baseAppointValGetAVLTreeNode(onlineList, requestClient);
                /* 给对方发送请求 todo.... */
                /* 暂时只要在线就回复同意请求 */
                int request = 1;
                write(acceptfd, &request, sizeof(request));
            }

            /* 不在线 */
            int request = 0;
            write(acceptfd, &request, sizeof(request));
#endif
            break;

        /* 删除好友 */
        case F_FRIEND_DELETE:
            /* code */
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
    ssize_t readChoice = 0;
    ssize_t readBytes = 0;
    ssize_t writeBytes = 0;

    char * errMsg = NULL;

    /* 新建用户结点 */
    clientNode client;
    bzero(&client, sizeof(clientNode));
    bzero(client.loginName, sizeof(DEFAULT_LOGIN_NAME));
    bzero(client.loginPawd, sizeof(DEFAULT_LOGIN_PAWD));

    client.communicateFd = acceptfd;

    /* 储存sql语句 */
    char sql[BUFFER_SQL];
    bzero(sql, sizeof(sql));

    /* 存储查询结果 */
    char ** result = NULL;
    int row = 0;
    int column = 0;

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
            int flag = 0;
            do {

                readName(acceptfd, &client);
                readPasswd(acceptfd, &client);

                result = NULL;
                row = 0;
                column = 0;
                /* 判断用户输入是否正确 */
                sprintf(sql, "select id = '%s' from user where password = '%s'", client.loginName, client.loginPawd);
                ret = sqlite3_get_table(g_chatRoomDB, sql, &result, &row, &column, &errMsg);
                if (ret != SQLITE_OK)
                {
                    printf("select error : %s\n", errMsg);
                    close(acceptfd);
                    pthread_exit(NULL);
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
                        close(acceptfd);
                        pthread_exit(NULL);
                    }
                    flag = 0;
                }
                else
                {
                    writeBytes = write(acceptfd, "用户名或密码输入错误，请重新输入\n", sizeof("用户名或密码输入错误，请重新输入\n"));
                    if (writeBytes < 0)
                    {
                        perror("write error");
                        close(acceptfd);
                        pthread_exit(NULL);
                    }
                    flag = 1;
                    continue;
                }

                chatRoomFunc(chat, &client);

                /* 打印在线列表 */
                balanceBinarySearchTreeLevelOrderTravel(onlineList);

                // if(chatRoomFunc(chat, &client) < 0)
                // {
                //     close(acceptfd);
                //     pthread_exit(NULL);
                // }

                choice = 0;

            }while (flag);
            
            break;
        
        /* 注册功能 */
        case REGISTER:
            flag = 0;
            do 
            {
                readName(acceptfd, &client);

                row = 0;
                column = 0;
                result = NULL;
                sprintf(sql, "select id from user where id = '%s'", client.loginName);
                printf("登录名：%s", client.loginName);
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

                    readPasswd(acceptfd, &client);

                    /* 插入数据库中 */
                    sprintf(sql, "insert into user values('%s', '%s')", client.loginName, client.loginPawd);
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

                    choice = 0;

                    flag = 0;
                }
            } while (flag);

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


    int socketfd = socket(AF_INET, SOCK_STREAM, 0);
    if (socketfd == -1)
    {
        perror("socket error");
        sqlite3_close(g_chatRoomDB);
        sqlite3_close(g_clientMsgDB);
        pthread_mutex_destroy(&g_mutex);
        // threadPoolDestroy(&pool);
        exit(-1);
    }

    struct sockaddr_in serverAddress;
    struct sockaddr_in clientAddress;

    /* 存储服务器信息 */
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(SERVER_PORT);
    serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);

    socklen_t serverAddressLen = sizeof(serverAddress);
    socklen_t clientAddressLen = sizeof(clientAddress);

    /* 设置端口复用 */
    int enableOpt = 1;
    ret = setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, (void *) &enableOpt, sizeof(enableOpt));
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