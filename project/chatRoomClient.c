#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <strings.h>
#include <sqlite3.h>
#include <string.h>
#include "chatRoomClient.h"
#include "balanceBinarySearchTree.h"
#include <json-c/json.h>
#include <pthread.h>
#include <json-c/json.h>
#include <semaphore.h>
#include <signal.h>

#define SERVER_PORT     8080
#define SERVER_ADDR     "172.31.173.216"
#define BUFFER_SIZE     300
#define BUFFER_SQL      100   
#define DEFAULT_CHAT    450

/* 通信和文件描述符 */
int socketfd = 0;
int mainMenu = 0;
int funcMenu = 0;

/* 初始化一个好友树和chat */
BalanceBinarySearchTree * friendTree = NULL;

/* 信号量 */
sem_t finish;

char chatBuffer11[DEFAULT_CHAT];

/* 主界面功能选择 */
enum CLIENT_CHOICE
{
    LOG_IN = 1,
    REGISTER,
    EXIT,
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

/* 运行状态码 */
enum STATUS_CODE
{
    ON_SUCCESS,
    ERROR = -2,
    NULL_FRIEND,
};


/* 前置声明 */
int chatRoomFunc(int socketfd, message * Msg);

/* 销毁资源 */
int destorySorce()
{
    close(mainMenu);
    close(socketfd);
    if (funcMenu)
    {
        close(funcMenu);
    }
    if (friendTree != NULL)
    {
        if (friendTree->size)
        {
            balanceBinarySearchTreeDestroy(friendTree);
        } 
    }
    
    sem_destroy(&finish);

    return ON_SUCCESS;
}

/* AVL比较器：以登录名做比较 */
int compareFunc(void * val1, void * val2)
{
    char * client = (char *)val1;
    char * data = (char *)val2;

    if (client > data)
    {
        return 1;
    }
    else if (client < data)
    {
        return -1;
    }


    return 0;
}

/* AVL打印器 */
int printfFunc(void * val)
{
    char * client = (char *)val;
    printf("id : %s\n", client);
}

/* 客户端的初始化 */
int chatRoomClientInit(int socketfd)
{
    struct sockaddr_in localAddress;
    
    localAddress.sin_family = AF_INET;
    localAddress.sin_port = htons(SERVER_PORT);
    int ret = inet_pton(AF_INET, SERVER_ADDR, &localAddress.sin_addr.s_addr);
    if (ret == -1)
    {
        perror("inet_pton error");
    }

    socklen_t localAddressLen = sizeof(localAddress);

    /* 客户端连接 */
    ret = connect(socketfd, (struct sockaddr *)&localAddress, localAddressLen);
    if (ret == -1)
    {
        perror("connect error");
    }

    return ON_SUCCESS;
}

/* 客户端的登录注册 */
int chatRoomClientLoginInRegister(int socketfd, message * Msg)
{
    ssize_t writeBytes = 0;
    char c = '0';
    int ret = 0;
    int flag = 0;

    do
    {
        bzero(Msg->clientLogInName, sizeof(Msg->clientLogInName));
        bzero(Msg->clientLogInPasswd, sizeof(Msg->clientLogInPasswd));
        
        /* 获取用户名添加到Msg结构体中 */
        printf("请输入你的登录名(不超过20个字符,输入q退出):\n");
        scanf("%s", Msg->clientLogInName);
        while ((c = getchar()) != EOF && c != '\n');

        /* 判断是否退出 */
        if (!strncmp(Msg->clientLogInName, "q", sizeof(Msg->clientLogInName)))
        {
            write(socketfd, "q", sizeof("q"));
            return ON_SUCCESS;
        }

        /* 获取密码添加到Msg结构体中 */
        printf("请输入你的登录密码(输入q退出)：\n");
        scanf("%s", Msg->clientLogInPasswd);
        while ((c = getchar()) != EOF && c != '\n');

        /* 判断是否退出 */
        if (!strncmp(Msg->clientLogInPasswd, "q", sizeof(Msg->clientLogInPasswd)))
        {
            write(socketfd, "q", sizeof("q"));
            return ON_SUCCESS;
        }

        /* 将结构体发送给服务器 */
        writeBytes = write(socketfd, Msg, sizeof(struct message));
        if (writeBytes < 0)
        {
            perror("write error");
            return ERROR;
        }

        ssize_t readBytes = read(socketfd, Msg, sizeof(struct  message));
        if(readBytes < 0)
        {
            perror("read error");
            //pthread_exit(NULL);
        }
        if (readBytes == 0)
        {
            printf("服务器正忙\n");
            return ERROR;
        }

        switch (Msg->choice)
        {
        /* 接收的登录消息 */
        case LOG_IN:
            if (!strncmp(Msg->message, "用户名或密码输入错误，请重新输入", sizeof("用户名或密码输入错误，请重新输入")))
            {
                printf("%s\n", Msg->message);
                flag = 1;
                continue;
            }
            else
            {
                printf("%s\n", Msg->message);
                flag = 0;
                /* 清屏 */
                system("clear");
                ret = chatRoomFunc(socketfd, Msg);
                if (ret != ON_SUCCESS)
                {
                    printf("chatRoomFunc error");
                    break;
                }
            }

            break;

        /* 接收的注册消息 */
        case REGISTER:
            if (!strncmp(Msg->message, "登录名已存在，请重新输入!", sizeof("登录名已存在，请重新输入!")))
            {
                printf("%s\n", Msg->message);
                flag = 1;
                continue;
            }
            else
            {
                printf("%s\n", Msg->message);
                flag = 0;
                /* 清屏 */
                system("clear");
                ret = chatRoomFunc(socketfd, Msg);
                if (ret != ON_SUCCESS)
                {
                    printf("chatRoomFunc error");
                    break;
                }
            }

            break;
        
        default:
            break;
        }

    } while (flag);
    
    return ON_SUCCESS;
}

#if 0
/* 从服务器读取好友列表 */
int readFriends(int socketfd, message * Msg)
{

    /* 只需要将msg里面的choice传过去就好 */
    ssize_t writeBytes = write(socketfd, Msg, sizeof(struct message));
    if (writeBytes == -1)
    {
        perror("write error in readFriends");
    }

    sem_wait(&finish);

    return ON_SUCCESS;
}
#endif

/* 打印好友列表 */
int printFrientList(int socketfd, BalanceBinarySearchTree * friendTree, message * Msg, int capacity)
{
    char *readBuffer = (char *)malloc(sizeof(char) * (capacity + 1));
    if (readBuffer == NULL)
    {
        perror("malloc error");
        return ERROR;
    }
    bzero(readBuffer, sizeof(char) * (capacity + 1));
    
    ssize_t readBytes = read(socketfd, readBuffer, sizeof(char) * (capacity + 1));
    if (readBytes < 0)
    {
        perror("read error");
        return ERROR;
    }
    else
    {
        /* 解析传过来的字符串 */
        struct json_object *friendList = json_tokener_parse(readBuffer);
        /* 得到数组 */
        json_object * jsonArray = json_object_object_get(friendList, "id");

        int len = Msg->choice;

        struct json_object * val = NULL;

        for (int idx = 0; idx < len; idx++)
        {
            val = json_object_array_get_idx(jsonArray, idx);
            balanceBinarySearchTreeInsert(friendTree, (void *)json_object_get_string(val));
        }

    }
    
    if (readBuffer)
    {
        free(readBuffer);
        readBuffer = NULL;
    } 

    return ON_SUCCESS;

} 

/* 添加好友 */
int chatRoomClientAddFriends(int socketfd,  BalanceBinarySearchTree * friendTree, message * Msg)
{

    bzero(Msg->requestClientName, sizeof(Msg->requestClientName));

    int choice = 0;
    char c = '0';


    ssize_t writeBytes = 0;

    printf("请输入你要添加的好友id(输入q退出):\n");
    scanf("%s", Msg->requestClientName);
    while ((c = getchar()) != EOF && c != '\n');
    printf("zheli\n");
    if (!strncmp(Msg->requestClientName, "q", sizeof(Msg->requestClientName)))
    {
        return ON_SUCCESS;
    }

    printf("zheli|n\n");
    /* 给添加的对象发送添加请求 */
    writeBytes = write(socketfd, Msg, sizeof(struct message));
    if (writeBytes < 0)
    {
        perror("write error");
        return ERROR;
    }

    sem_wait(&finish);

    printf("这届\n");

    return ON_SUCCESS;
}

/* 删除好友 */
int chatRoomDeleteFriends(int socketfd, BalanceBinarySearchTree * friendTree, message * Msg)
{
    balanceBinarySearchTreeInOrderTravel(friendTree);

    bzero(Msg->requestClientName, sizeof(Msg->requestClientName));
    printf("请输入你要删除的好友id:(输入q退出)\n");
    scanf("%s", Msg->requestClientName);
    if (!strncmp(Msg->requestClientName, "q", sizeof(Msg->requestClientName)))
    {
        return ON_SUCCESS;
    }

    write(socketfd, Msg, sizeof(struct message));

    int ret = balanceBinarySearchTreeDelete(friendTree, Msg->requestClientName);
    if (ret != 0)
    {
        printf("客户端删除好友失败");
    }
    
    return ON_SUCCESS;
}

/* 私聊发送信息 */
int chatRoomPrivateChat(message * Msg, int socketfd)
{
    char c = '0';

    while (1)
    {
        scanf("%s", Msg->message);
        while ((c = getchar()) != EOF && c != '\n');
        if(!strncmp(Msg->message, "q", sizeof(Msg->message)))
        {
            write(socketfd, Msg, sizeof(struct message));
            return ON_SUCCESS;
        }

        write(socketfd, Msg, sizeof(struct message));
    }

    return ON_SUCCESS;
}

/* 创建群聊 */
int chatRoomClientCreateGroupChat(int socketfd, message * Msg)
{
    /* 创建群聊 */
    bzero(Msg->clientGroupName, sizeof(Msg->clientGroupName));

    char c = '0';
    ssize_t writeBytes = 0;

    printf("请输入你想要创建的群聊名称：\n");
    // scanf("%s", nameBuffer);
    scanf("%s", Msg->clientGroupName);

    while ((c = getchar()) != EOF && c != '\n');

    // strncpy(Msg->clientGroupName, nameBuffer, sizeof(nameBuffer));

    writeBytes = write(socketfd, Msg, sizeof(struct message));
    printf("Msg->clientGroupName :  %s\n", Msg->clientGroupName);
    

    if (writeBytes < 0)
    {
        perror("write error");
        return ERROR;
    }

    return ON_SUCCESS;
       
}

/* 邀请好友进群 */
int chatRoomClientAddPeopleInGroup(int socketfd, message * Msg, BalanceBinarySearchTree * friendTree)
{
#if 1
            
    char c = '0';

    printf("好友id:   \n");
    memset(Msg->requestClientName, 0, sizeof(Msg->requestClientName));
    scanf("%s", Msg->requestClientName);
    while ((c = getchar()) != EOF && c != '\n');

    int ret = balanceBinarySearchTreeIsContainAppointVal(friendTree, (void *)Msg->requestClientName);
    if (ret == 0)
    {
        printf("没有该好友，请重新输入！");
        bzero(Msg->requestClientName, sizeof(Msg->requestClientName));
        scanf("%s", Msg->requestClientName);
        while ((c = getchar()) != EOF && c != '\n');
    }
    else
    {
        printf("请选择要拉的群聊名称==\n");
        bzero(Msg->clientGroupName, sizeof(Msg->clientGroupName));
        scanf("%s", Msg->clientGroupName);
        /* 将好友id发送到服务器 */
        write(socketfd, Msg, sizeof(struct message));

        // char chatReadBuffer[BUFFER_CHAT];

        // bzero(chatReadBuffer, sizeof(chatReadBuffer));
        // read(socketfd, chatReadBuffer, sizeof(chatReadBuffer));

        // if(strncmp(chatReadBuffer, "q", sizeof(chatReadBuffer)) == 0)
        // {
        //     printf("添加成功！");
        // }
    }
#endif
}

/* 发起群聊 */
int chatRoomClientStartGroupCommunicate()
{
#if 0
    int c = '0';
    ssize_t readBytes = 0;

    char writeBuffer[DEFAULT_LOGIN_NAME];
    bzero(writeBuffer, sizeof(writeBuffer));

    char readBuffer[DEFAULT_LOGIN_NAME];
    bzero(readBuffer, sizeof(readBuffer));

    struct json_object * groupHistory = json_object_new_object();

    printf("发起群聊的名称:   \n");
    scanf("%s", writeBuffer);
    while ((c = getchar()) != EOF && c != '\n');

    struct json_object * groupNameVal = json_object_new_string(writeBuffer);
    json_object_object_add(groupHistory, "groupName", groupNameVal);
    struct json_object * id = json_object_new_string(client->loginName);
    json_object_object_add(groupHistory, "id", id);

    bzero(writeBuffer, sizeof(writeBuffer));
    printf("发送:   \n");
    scanf("%s", writeBuffer);
    while ((c = getchar()) != EOF && c != '\n');
    struct json_object * historyVal = json_object_new_string(writeBuffer);
    json_object_object_add(groupHistory, "history", historyVal);

    const char * groupHistoryVal = json_object_to_json_string(groupHistory);
    write(socketfd, groupHistoryVal, strlen(groupHistoryVal));

    readBytes = read(socketfd, readBuffer, sizeof(readBuffer));
    if (readBytes <= 0)
    {
        perror("read error");
        exit(-1);
    }
    else
    {
        printf("read : %s\n", readBuffer);
    }
      
#endif
}

/* 功能界面接收消息 */
void * read_message(void * arg)
{
    /* 线程分离 */
    pthread_detach(pthread_self());

    /* 拿到已保存信息的chatRoom */
    chatRoom * chat = (chatRoom *)arg;
    int socketfd = chat->socketfd;
    BalanceBinarySearchTree * friendTree = chat->friend;

    ssize_t readBytes = 0;

    /* 接收信息体 */
    message Msg;
    bzero(&Msg, sizeof(Msg));
    bzero(Msg.clientLogInName, sizeof(Msg.clientLogInName));
    bzero(Msg.clientLogInPasswd, sizeof(Msg.clientLogInPasswd));
    bzero(Msg.message, sizeof(Msg.message));

    while (1)
    {
        readBytes = read(socketfd, &Msg, sizeof(Msg));
        if (readBytes < 0)
        {
            perror("read message error");
            pthread_exit(NULL);
        }
        if (readBytes == 0)
        {
            printf("服务器正忙\n");
            kill(getpid(), SIGINT);
            pthread_exit(NULL);
        }
        //测试...............................................
        printf("Msg.message:%s\n", Msg.message);

        switch (Msg.func_choice)
        {
        /* 查看好友 */
        case F_FRIEND_VIEW:
            
            break;
        
        /* 添加好友 */
        case F_FRIEND_INCREASE:
            if (!strncmp(Msg.message, "对方同意了您的请求", sizeof(Msg.message)))
            {
                /* 同意请求,插入好友树 */
                balanceBinarySearchTreeInsert(friendTree, Msg.requestClientName);
                /* 测试......................................................................................... */
                balanceBinarySearchTreeInOrderTravel(friendTree);
            }
            else if (!strncmp(Msg.message, "对方已经是您的好友", sizeof(Msg.message)))
            {
                printf("%s\n", Msg.message);
            }
            else
            {
                printf("%s\n", Msg.message);
                chatRoomClientAddFriends(socketfd, friendTree, &Msg);
            }
            
            sem_post(&finish);

            break;
        
        /* 删除好友 */
        case F_FRIEND_DELETE:
            if (!strncmp(Msg.message, "未找到该好友，请检查是否输入正确", sizeof(Msg.message)))
            {
                chatRoomDeleteFriends(socketfd, friendTree, &Msg);
            }
            else
            {
                printf("删除好友成功\n");
            }
            break;

        /* 私聊 */
        case F_PRIVATE_CHAT:

            if (!strncmp(Msg.message, "对方在线，可以输入要传送的消息", sizeof(Msg.message)))
            {
                /* 在线发起私聊 */
                chatRoomPrivateChat(&Msg, socketfd);
            }
            else
            {
                printf("%s\n", Msg.message);
            }

            break;

        /* 创建群聊 */
        case F_CREATE_GROUP:
            if (!strncmp(Msg.message, "创建群聊成功", sizeof(Msg.message)))
            {
                printf("群聊已存在！");
                /* 跳转到功能界面 */
                /* todo... */
            }
            else
            {
                printf("%s\n", Msg.message);
            }

            break;

        /* 群聊 */
        case F_GROUP_CHAT:

            break;
        
        default:
            break;
        }
    }

}

/* 聊天室功能 */
int chatRoomFunc(int socketfd, message * Msg)
{
    ssize_t readBytes = 0;

    /* 登录后/注册后打开功能页面 */
    funcMenu = open("funcMenu", O_RDONLY);
    if (funcMenu == -1)
    {
        perror("open funcMenu error");
        return ERROR;
    }
    char funcMenuBuffer[BUFFER_SIZE];
    bzero(funcMenuBuffer, sizeof(funcMenuBuffer));

    readBytes = read(funcMenu, funcMenuBuffer, sizeof(funcMenuBuffer));
    if (readBytes < 0)
    {
        perror("read error\n");
        close(funcMenu);
        return ERROR;
    }

    balanceBinarySearchTreeInit(&friendTree, compareFunc, printfFunc);

    chatRoom chat;
    bzero(&chat, sizeof(chat));

    chat.socketfd = socketfd;
    chat.friend = friendTree;

    char c = '0';
    int ret = 0;
    
    Msg->func_choice = F_FRIEND_VIEW;
    write(socketfd, Msg, sizeof(struct message));

    ret = read(socketfd, Msg, sizeof(struct message));
    if (ret < 0)
    {
        close(funcMenu);
        return ERROR;
    }

    /* 插入到好友树 */
    printFrientList(socketfd, friendTree, Msg, Msg->func_choice);
    Msg->func_choice = 0;

    /* 创建读线程 -- 主要读Msg.message ---服务器的回复 */
    pthread_t readTid;
    pthread_create(&readTid, NULL, read_message, &chat);

    while (1)
    {
        printf("%s\n", funcMenuBuffer);
        printf("请输入你需要的功能：\n");
        scanf("%d", &Msg->func_choice);
        while ((c = getchar()) != EOF && c != '\n');

        switch (Msg->func_choice)
        {
        /* 查看好友 */
        case F_FRIEND_VIEW:
            balanceBinarySearchTreeInOrderTravel(friendTree);
            
            break;

        /* 添加好友 */
        case F_FRIEND_INCREASE:
            chatRoomClientAddFriends(socketfd, friendTree, Msg);
           
            break;

        /* 删除好友 */
        case F_FRIEND_DELETE:
            chatRoomDeleteFriends(socketfd, friendTree, Msg);

            break;

        /* 私聊 */
        case F_PRIVATE_CHAT:
            printf("请选择要私聊的对象:\n");
            scanf("%s", Msg->requestClientName);
            while ((c = getchar()) != EOF && c != '\n');

            write(socketfd, Msg, sizeof(struct message));

            break;

        /* 创建群聊 */
        case F_CREATE_GROUP:
            chatRoomClientCreateGroupChat(socketfd, Msg);
            break;
        case F_INVITE_GROUP:
            chatRoomClientAddPeopleInGroup(socketfd, Msg, friendTree);
        break;
        /* 群聊 */
        case F_GROUP_CHAT:

            break;
        
        default:
            break;
        }

        if (Msg->func_choice == F_EXIT)
        {
            write(socketfd, Msg, sizeof(struct message));
            return ON_SUCCESS;
        }

    }

    return  ON_SUCCESS;
}

void sighander(int sig)
{
    printf("正在下线...\n");
    destorySorce();
    exit(0);
}

int main()
{

    signal(SIGINT, sighander);

    int ret = 0;
    socketfd = socket(AF_INET, SOCK_STREAM, 0);
    if (socketfd == -1)
    {
        perror("socket error");
        exit(-1);
    }

    chatRoomClientInit(socketfd);

    sem_init(&finish, 0, 0);


    /* 打开主菜单文件 ---读文件 */
    mainMenu = open("mainMenu", O_RDONLY);
    if (mainMenu == -1)
    {
        perror("open mainMenu error");
        close(socketfd);
        exit(-1);
    }
    char mainMenuBuffer[BUFFER_SIZE];
    bzero(mainMenuBuffer, sizeof(mainMenuBuffer));
    read(mainMenu, mainMenuBuffer, sizeof(mainMenuBuffer) - 1);

    /* 新建消息结构体 */
    message Msg;
    bzero(&Msg, sizeof(Msg));
    bzero(Msg.clientLogInName, sizeof(Msg.clientLogInName));
    bzero(Msg.clientLogInPasswd, sizeof(Msg.clientLogInPasswd));

    ssize_t writeBytes = 0;
    char c = '0';

    /* 开始执行功能 */
    while (1)
    {
        printf("%s\n", mainMenuBuffer);
        printf("请选择你需要的功能：\n");
        scanf("%d", &Msg.choice);
        while ((c = getchar()) != EOF && c != '\n');

        switch (Msg.choice)
        {

        /* 登录 */
        case LOG_IN:

            ret = chatRoomClientLoginInRegister(socketfd, &Msg);
            if (ret != ON_SUCCESS)
            {
                close(socketfd);
                close(mainMenu);
                sem_destroy(&finish);
                exit(-1);
            }

            break;
        
        /* 注册 */
        case REGISTER:
            ret = chatRoomClientLoginInRegister(socketfd, &Msg);
            if (ret != ON_SUCCESS)
            {
                close(socketfd);
                close(mainMenu);
                sem_destroy(&finish);
                exit(-1);
            }
            break;

        default:
            break;
        }

        /* 退出主界面 */
        if (Msg.choice == EXIT)
        {
            write(socketfd, &Msg, sizeof(Msg));
            break;
        }
        
    }

    /* 关闭资源 */
    close(socketfd);
    close(mainMenu);
    sem_destroy(&finish);
    

    return 0;
}