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

#define SERVER_PORT     8080
#define LISTEN_MAX      128

#define MIN_THREADS     10
#define MAX_THREADS     20
#define MAX_QUEUE_CAPACITY  10

#define DEFAULT_LOGIN_NAME  20
#define DEFAULT_LOGIN_PAWD  16
#define BUFFER_SQL          100

/* 创建数据库句柄 */
sqlite3 * chatRoomDB = NULL;

enum CLIENT_CHOICE
{
    LOG_IN = 1,
    REGISTER,
    PRIVATE_CHAT,
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
} clientNode;


/* 锁 */
pthread_mutex_t g_mutex;

/* AVL比较器：以登录名做比较 */
int compareFunc(void * val1, void * val2)
{
    clientNode * client = (clientNode *)val1;
    clientNode * data = (clientNode *)val2;

    if (client->loginName > data->loginPawd)
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

void readNamePasswd(int acceptfd, struct clientNode * client)
{
    ssize_t readBytes = read(acceptfd, client->loginName, sizeof(client->loginName));
    if (readBytes < 0)
    {
        perror("read error");
        close(acceptfd);
        pthread_exit(NULL);
    }

    printf("登录名：%s\n", client->loginName);

    readBytes = read(acceptfd, client->loginPawd, sizeof(client->loginPawd));
    if (readBytes < 0)
    {
        perror("read error");
        close(acceptfd);
        sqlite3_close(chatRoomDB);
        pthread_exit(NULL);
    }

    printf("登录密码：%s\n", client->loginPawd);
}

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
    // sqlite3 * chatRoomDB = chat->ppDb;

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

    /* 储存sql语句 */
    char sql[BUFFER_SQL];
    bzero(sql, sizeof(sql));

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
            readNamePasswd(acceptfd, &client);

            /* 插入在线列表 */
            balanceBinarySearchTreeInsert(onlineList, &client);

            /* 打印在线列表 */
            balanceBinarySearchTreeLevelOrderTravel(onlineList);

            choice = 0;

            break;
        
        /* 注册功能 */
        case REGISTER:
            readNamePasswd(acceptfd, &client);

            sprintf(sql, "insert into user values('%s', '%s')", client.loginName, client.loginPawd);
            ret = sqlite3_exec(chatRoomDB, sql, NULL, NULL, &errMsg);
            if (ret != SQLITE_OK)
            {
                printf("insert error: %s \n", errMsg);
                close(acceptfd);
                sqlite3_close(chatRoomDB);
                pthread_exit(NULL);
            }

            writeBytes = write(acceptfd, "注册成功！", sizeof("注册成功！"));
            if (writeBytes == -1)
            {
                printf("write error:%s\n", errMsg);
                close(acceptfd);
                sqlite3_close(chatRoomDB);
                pthread_exit(NULL);
            }

            /* 插入在线列表 */
            balanceBinarySearchTreeInsert(onlineList, &client);

            /* 打印在线列表 */
            balanceBinarySearchTreeLevelOrderTravel(onlineList);

            choice = 0;

            break;

        default:
            break;
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
    int ret = sqlite3_open("chatRoom.db", &chatRoomDB);
    if (ret != SQLITE_OK)
    {
        perror("sqlite open error");
        exit(-1);
    }

    /* 创建储存所有用户的表 */
    char * ermsg = NULL;
    const char * sql = "create table if not exists user (id text primary key not null, password text not null)";
    ret = sqlite3_exec(chatRoomDB, sql, NULL, NULL, &ermsg);
    if (ret != SQLITE_OK)
    {
        printf("create table error:%s\n", ermsg);
        sqlite3_close(chatRoomDB);
        pthread_mutex_destroy(&g_mutex);
        // threadPoolDestroy(&pool);
        exit(-1);
    }

    int socketfd = socket(AF_INET, SOCK_STREAM, 0);
    if (socketfd == -1)
    {
        perror("socket error");
        sqlite3_close(chatRoomDB);
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
        sqlite3_close(chatRoomDB);
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
        sqlite3_close(chatRoomDB);
        pthread_mutex_destroy(&g_mutex);
        // threadPoolDestroy(&pool);
        exit(-1);
    }

    ret = listen(socketfd, LISTEN_MAX);
    if (ret == -1)
    {
        perror("listen error");
        close(socketfd);
        sqlite3_close(chatRoomDB);
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
        sqlite3_close(chatRoomDB);
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
    sqlite3_close(chatRoomDB);
    // threadPoolDestroy(&pool);

    return 0;
}