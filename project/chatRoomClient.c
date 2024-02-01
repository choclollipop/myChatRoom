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



#define SERVER_PORT 8080
#define SERVER_ADDR "172.28.25.146"
#define BUFFER_SIZE 300
#define BUFFER_SQL  100   
#define BUFFER_CHAT 401

#define DEFAULT_LOGIN_NAME  21
#define DEFAULT_LOGIN_PAWD  17 
#define DEFAULT_CHAT        450

/* 建立数据库句柄 */
sqlite3 * g_chatRoomDB = NULL;
sqlite3 * g_clientMsgDB = NULL;

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
    F_GROUP_CHAT,
};

/* 运行状态码 */
enum STATUS_CODE
{
    ON_SUCCESS,
    ERROR = -2,
    NULL_FRIEND,
};

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
    printf("id : %s\n", client->loginName);
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
        close(socketfd);
        exit(-1);
    }

    socklen_t localAddressLen = sizeof(localAddress);

    ret = connect(socketfd, (struct sockaddr *)&localAddress, localAddressLen);
    if (ret == -1)
    {
        perror("connect error");
        close(socketfd);
        exit(-1);
    }

    return ON_SUCCESS;
}

/* 客户端的登录 */
int chatRoomClientLoginIn(int socketfd, clientNode *client)
{
    ssize_t writeBytes = 0;
    ssize_t readBytes = 0;
    char c = '0';

    /* 读缓冲区 */
    char buffer[BUFFER_SIZE];
    bzero(buffer, sizeof(buffer));

    int flag = 0;
    do
    {
        printf("请输入你的登录名(不超过20个字符):\n");
        scanf("%s", client->loginName);
        while ((c = getchar()) != EOF && c != '\n');

        writeBytes = write(socketfd, client->loginName, sizeof(client->loginName));
        if (writeBytes < 0)
        {
            perror("write error");
            close(socketfd);
            exit(-1);
        }

        printf("请输入你的登陆密码：\n");
        scanf("%s", client->loginPawd);
        while ((c = getchar()) != EOF && c != '\n');

        writeBytes = write(socketfd, client->loginPawd, sizeof(client->loginPawd));
        if (writeBytes < 0)
        {
            perror("write error");
            close(socketfd);
            exit(-1);
        }

        readBytes = read(socketfd, buffer, sizeof(buffer));
        if (readBytes < 0)
        {
            perror("read error");
            close(socketfd);
            exit(-1);
        }

        int ret = strncmp("登录成功！\n", buffer, sizeof("登录成功！\n"));
        if (ret == 0)
        {
            /* 相等，登录成功 */
            printf("%s", buffer);
            flag = 0;

            break;
        }
        else
        {
            printf("%s", buffer);
            flag = 1;
        }

    } while (flag);
}

/* 客户端的注册 */
int chatRoomClientRegister(int socketfd, clientNode *client)
{
    ssize_t writeBytes = 0;
    ssize_t readBytes = 0;
    char c = '0';

    /* 读缓冲区 */
    char buffer[BUFFER_SIZE];
    bzero(buffer, sizeof(buffer));

    int flag = 0;
    do
    {
        printf("请输入你的登录名(不超过20个字符):\n");
        scanf("%s", client->loginName);
        while ((c = getchar()) != EOF && c != '\n');

        writeBytes = write(socketfd, client->loginName, sizeof(client->loginName));
        if (writeBytes < 0)
        {
            perror("write error");
            close(socketfd);
            exit(-1);
        }

        bzero(buffer, sizeof(buffer));
        readBytes = read(socketfd, buffer, sizeof(buffer));
        if (readBytes < 0)
        {
            perror("read error");
            close(socketfd);
            exit(-1);
        }

        int ret = strncmp("登录名已存在，请重新输入!\n", buffer, sizeof("登录名已存在，请重新输入!\n"));
        if (ret == 0)
        {
            printf("登录名已存在，请重新输入!\n");
            flag = 1;
            continue;
        }
        else
        {
            printf("%s", buffer);
            scanf("%s", client->loginPawd);
            while ((c = getchar()) != EOF && c != '\n');

            writeBytes = write(socketfd, client->loginPawd, sizeof(client->loginPawd));
            if (writeBytes < 0)
            {
                perror("write error");
                close(socketfd);
                exit(-1);
            }

            readBytes = read(socketfd, buffer, sizeof(buffer));
            if (readBytes < 0)
            {
                perror("write error");
                close(socketfd);
                exit(-1);
            }
            printf("%s\n", buffer);
            
            if (readBytes > 0)
            {
                break;
            }

            flag = 1;
        }   

    } while(flag);
            

}

/* 从服务器读取好友列表 */
int readFriends(int socketfd, BalanceBinarySearchTree * friendTree)
{
    ssize_t readBytes = 0;

    const char * friendListVal = NULL; 

    int row = 0;
    readBytes = read(socketfd, &row, sizeof(int) * row);
    if (readBytes < 0)
    {
        perror("read error");
        return ERROR;
    }

    if (row == 0)
    {
        printf("当前没有好友！\n");
    }
    else
    {
        while (1)
        {
            clientNode friend;
            readBytes = read(socketfd, friendListVal, strlen(friendListVal));
            if (readBytes < 0)
            {
                perror("read error");
                return ERROR;
            }
            else
            {
                struct json_object *friendList = json_tokener_parse(friendListVal);
                struct json_object *id = json_object_object_get(friendListVal, "id");
                balanceBinarySearchTreeInsert(friendTree, (void *)&id);
            }
            balanceBinarySearchTreeInOrderTravel(friendTree);
        }
    }
    
    return ON_SUCCESS;
}

/* 添加好友 */
int chatRoomClientAddFriends(int socketfd,  BalanceBinarySearchTree * friendTree)
{
    char nameBuffer[DEFAULT_LOGIN_NAME];
    bzero(nameBuffer, sizeof(nameBuffer));

    // char writeBuffer[BUFFER_SIZE];
    // bzero(writeBuffer, sizeof(writeBuffer));

    int choice = 0;
    char c = '0';

    printf("请输入你要添加的好友id:\n");
    scanf("%s", nameBuffer);
    while ((c = getchar()) != EOF && c != '\n');

    ssize_t writeBytes = 0;
    ssize_t readBytes = 0;

    /* 给添加的对象发送添加请求 */
    writeBytes = write(socketfd, nameBuffer, sizeof(nameBuffer));
    if (writeBytes < 0)
    {
        perror("write error");
        return ERROR;
    }

    int Agree = 0;
    readBytes = read(socketfd, &Agree, sizeof(Agree));
    if (Agree == 1)
    {
        /* 同意 */
        printf("对方已同意您的请求\n");
        
    }
    else if (Agree == 2)
    {
        printf("对方拒绝了您的请求\n");
    }
    else
    {
        printf("对方暂时不在线\n");
    }


}

/* 群聊 */
int chatRoomClientGroupChat(int socketfd, clientNode *client, BalanceBinarySearchTree * friendTree)
{
    /* 创建群聊 */
    char nameBuffer[DEFAULT_LOGIN_NAME];
    bzero(nameBuffer, sizeof(nameBuffer));

    char c = '0';
    int flag = 0;

    printf("请输入你想要创建的群聊名称：\n");
    scanf("%s", nameBuffer);
    while ((c = getchar()) != EOF && c != '\n');

    /* 将群聊名称和登录用户名称传到服务器 */
    struct json_object * group = json_object_new_object();
    struct json_object * groupName = json_object_new_string(nameBuffer);
    struct json_object * id = json_object_new_string(client->loginName);

    json_object_object_add(group, "groupName", groupName);
    json_object_object_add(group, "id", id);

    const char * groupVal = json_object_to_json_string(group);
    write(socketfd, groupVal, strlen(groupVal));

    read(socketfd, &flag, sizeof(int));
    if (flag == 1)
    {
        printf("创建群聊成功！\n");
        /* 拉人进群 */
        chatRoomClientAddPeopleInGroup(socketfd, client, friendTree);
    }    
}

/* 拉人进群 */
int chatRoomClientAddPeopleInGroup(int socketfd, clientNode *client, BalanceBinarySearchTree * friendTree)
{
    char idBuffer[DEFAULT_LOGIN_NAME];
    bzero(idBuffer, sizeof(idBuffer));

    char c = '0';

    printf("好友id:   \n");
    scanf("%s", idBuffer);
    while ((c = getchar()) != EOF && c != '\n');

    int ret = balanceBinarySearchTreeIsContainAppointVal(friendTree, (void *)idBuffer);
    if (ret == 0)
    {
        printf("没有该好友，请重新输入！");
        bzero(idBuffer, sizeof(idBuffer));
        scanf("%s", idBuffer);
        while ((c = getchar()) != EOF && c != '\n');
    }
    else
    {
        /* 将好友id发送到服务器 */
        write(socketfd, idBuffer, sizeof(idBuffer));

        char chatReadBuffer[BUFFER_CHAT];

        bzero(chatReadBuffer, sizeof(chatReadBuffer));
        read(socketfd, chatReadBuffer, sizeof(chatReadBuffer));

        if(strncmp(chatReadBuffer, "q", sizeof(chatReadBuffer)) == 0)
        {
            printf("添加成功！");
        }
    }
}

/* 发起群聊 */
int chatRoomClientStartGroupCommunicate(int socketfd, clientNode *client, BalanceBinarySearchTree * friendTree)
{
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
      

}

/* 接收消息线程 */
void * recv_message(void * arg)
{
    pthread_detach(pthread_self());
    int socketfd = *(int *)arg;

    char chatBuffer[DEFAULT_CHAT];
    bzero(chatBuffer, sizeof(chatBuffer));

    while (1)
    {
        bzero(chatBuffer, sizeof(chatBuffer));
        read(socketfd, chatBuffer, sizeof(chatBuffer));

        printf("%s\n", chatBuffer);
    }

    pthread_exit(NULL);
}

/* 聊天室功能 */
int chatRoomFunc(int socketfd, const clientNode* client)
{
    ssize_t writeBytes = 0;
    ssize_t readBytes = 0;

    /* 登录后/注册后打开功能页面 */
    int funcMenu = open("funcMenu", O_RDONLY);
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

    BalanceBinarySearchTree * friendTree;
    balanceBinarySearchTreeInit(&friendTree, compareFunc, printfFunc);

    int choice = 0;
    char c = '0';
    int ret = 0;

    char nameBuffer[DEFAULT_LOGIN_NAME];
    bzero(nameBuffer, sizeof(nameBuffer));

    char writeBuffer[BUFFER_SIZE];
    bzero(writeBuffer, sizeof(writeBuffer));

    /* 接收消息线程 */
    pthread_t recvtid;
    pthread_create(&recvtid, NULL, recv_message, (void *)&socketfd);

    while (1)
    {
        printf("%s\n", funcMenuBuffer);
        printf("请输入你需要的功能：\n");
        scanf("%d", &choice);
        while ((c = getchar()) != EOF && c != '\n');

        /* ⭐发送选择的功能 */
        writeBytes = write(socketfd, &choice, sizeof(choice));
        if (writeBytes < 0)
        {
            perror("write error");
            close(funcMenu);
            return ERROR;
        }

        switch (choice)
        {
        /* 查看好友 */
        case F_FRIEND_VIEW:
            readFriend(socketfd, friendTree);
            break;

        /* 添加好友 */
        case F_FRIEND_INCREASE:
            chatRoomClientAddFriends(socketfd, friendTree);
            // printf("请输入你要添加的好友id:\n");
            // scanf("%s", nameBuffer);
            // while ((c = getchar()) != EOF && c != '\n');
           
            break;
         
        case F_CREATE_GROUP:
        chatRoomClientGroupChat(socketfd, client, friendTree);

            break;

        case F_PRIVATE_CHAT:
            printf("请选择要私聊的对象:\n");
            scanf("%s", nameBuffer);
            while ((c = getchar()) != EOF && c != '\n');

            /* 创建私聊json对象 */
            struct json_object * clientObj = json_object_new_object();
            if (clientObj == NULL)
            {
                perror("new jsonObject error");
                return ERROR;
            }

            ret = json_object_object_add(clientObj, client->loginName, json_object_new_string(nameBuffer));
            if (ret < 0)
            {
                free(clientObj);
                clientObj = NULL;
                perror("add nameValue error");
                return ERROR;
            }

            /* 转成字符串 */
            const char * clientPtr = json_object_to_json_string(clientObj);
            int len = strlen(clientPtr);
            write(socketfd, &len, sizeof(len));

            writeBytes = write(socketfd, clientPtr, strlen(clientPtr) + 1);
            if (writeBytes < 0)
            {
                free(clientObj);
                clientObj = NULL;
                close(socketfd);
                close(funcMenu);
                return ERROR;
            }

            /* 关闭json对象 */
            json_object_put(clientObj);

            usleep(500);
            char chatWriteBuffer[BUFFER_CHAT];

            while (1)
            {
                bzero(chatWriteBuffer, sizeof(chatWriteBuffer));
                scanf("%s", chatWriteBuffer);
                while ((c = getchar()) != EOF && c != '\n');
                if(!strncmp(chatWriteBuffer, "q", sizeof(chatWriteBuffer)))
                {
                    write(socketfd, "q", sizeof("q"));
                    break;
                }

                write(socketfd, chatWriteBuffer, sizeof(chatWriteBuffer));

            }

            break;
        
        default:
            break;
        }
    }

    return  ON_SUCCESS;
}


int main()
{
    int socketfd = socket(AF_INET, SOCK_STREAM, 0);
    if (socketfd == -1)
    {
        perror("socket error");
        exit(-1);
    }

    chatRoomClientInit(socketfd);


    /* 打开主菜单文件 */
    int mainMenu = open("mainMenu", O_RDONLY);
    if (mainMenu == -1)
    {
        perror("open mainMenu error");
        close(socketfd);
        exit(-1);
    }
    char mainMenuBuffer[BUFFER_SIZE];
    bzero(mainMenuBuffer, sizeof(mainMenuBuffer));
    read(mainMenu, mainMenuBuffer, sizeof(mainMenuBuffer) - 1);

    int choice = 0;
    int funchoice = 0;
    char c = '0';

    /* 新建用户结点 */
    clientNode client;
    bzero(&client, sizeof(clientNode));
    bzero(client.loginName, sizeof(DEFAULT_LOGIN_NAME));
    bzero(client.loginPawd, sizeof(DEFAULT_LOGIN_PAWD));

#if 0
    /* 打开数据库 */
    int ret = sqlite3_open("chatRoom.db", &g_chatRoomDB);
    if (ret != SQLITE_OK)
    {
        perror("sqlite open error");
        close(mainMenu);
        close(socketfd);
        exit(-1);
    }

    char ** result = NULL;
    int row = 0;
    int column = 0;

    /* 数据库语句 */
    char sql[BUFFER_SQL];
    bzero(sql, sizeof(sql));

#endif
    ssize_t writeBytes = 0;



    /* 开始执行功能 */
    while (1)
    {
        printf("%s\n", mainMenuBuffer);
        printf("请选择你需要的功能：\n");
        scanf("%d", &choice);
        /* 清空输入缓冲区 */
        writeBytes = write(socketfd, &choice, sizeof(choice));
        if (writeBytes < 0)
        {
            perror("write error");
            close(mainMenu);
            close(socketfd);
            exit(-1);
        }
        while ((c = getchar()) != EOF && c != '\n');

        switch (choice)
        {

        /* 登录 */
        case LOG_IN:

            chatRoomClientLoginIn(socketfd, &client);

            break;
        
        /* 注册 */
        case REGISTER:
            chatRoomClientRegister(socketfd, &client);
            break;

        default:
            break;
        }

        /* 退出主界面 */
        if (choice == EXIT)
        {
            break;
        }
        
    }

    /* 关闭资源 */
    close(socketfd);
    sqlite3_close(g_chatRoomDB);
    close(mainMenu);
    

    return 0;
}