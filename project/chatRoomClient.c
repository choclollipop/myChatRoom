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

#define SERVER_PORT 8080
#define SERVER_ADDR "172.30.149.120"
#define BUFFER_SIZE 300
#define BUFFER_SQL  100   

#define DEFAULT_LOGIN_NAME  21
#define DEFAULT_LOGIN_PAWD  17 

enum CLIENT_CHOICE
{
    LOG_IN = 1,
    REGISTER,
    EXIT,
};

enum FUNC_CHOICE
{
    PRIVATE_CHAT = 1,
};

typedef struct clientNode
{
    char loginName[DEFAULT_LOGIN_NAME];
    char loginPawd[DEFAULT_LOGIN_PAWD];
} clientNode;

int chatRoomFunc(int socketfd)
{
    /* 登录后/注册后打开功能页面 */
    int funcMenu = open("funcMenu", O_RDONLY);
    if (funcMenu == -1)
    {
        perror("open funcMenu error");
        exit(-1);
    }
    char funcMenuBuffer[BUFFER_SIZE];
    bzero(funcMenuBuffer, sizeof(funcMenuBuffer));
    read(funcMenu, funcMenuBuffer, sizeof(funcMenuBuffer) - 1);

    int choice = 0;
    char c = '0';

    while (1)
    {
        printf("%s\n", funcMenuBuffer);
        printf("请输入你需要的功能：\n");
        scanf("%d", &choice);
        while ((c = getchar()) != EOF && c != '\n');

        switch (choice)
        {
        case PRIVATE_CHAT:
            printf("请输入你想建立私聊的人选：\n");
            break;
        
        default:
            break;
        }
    }
}

int main()
{
    int socketfd = socket(AF_INET, SOCK_STREAM, 0);
    if (socketfd == -1)
    {
        perror("socket error");
        exit(-1);
    }

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

    /* 打开数据库 */
    sqlite3 * chatRoomDB = NULL;
    /* 打开数据库 */
    ret = sqlite3_open("chatRoom.db", &chatRoomDB);
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

    /* 存储数据库错误信息 */
    char * errMsg = NULL;

    ssize_t writeBytes = 0;
    ssize_t readBytes = 0;

    /* 读缓冲区 */
    char buffer[BUFFER_SIZE];
    bzero(buffer, sizeof(buffer));

    /* 数据库语句 */
    char sql[BUFFER_SQL];
    bzero(sql, sizeof(sql));

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
            printf("请输入你的登录名(不超过20个字符):\n");
            scanf("%s", client.loginName);
            while ((c = getchar()) != EOF && c != '\n');

            printf("请输入你的登陆密码：\n");
            scanf("%s", client.loginPawd);
            while ((c = getchar()) != EOF && c != '\n');

            sprintf(sql, "select id = '%s' from user where password = '%s'", client.loginName, client.loginPawd);
            ret = sqlite3_get_table(chatRoomDB, sql, &result, &row, &column, &errMsg);
            if (ret != SQLITE_OK)
            {
                printf("select error : %s\n", errMsg);
                close(mainMenu);
                close(socketfd);
                exit(-1);
            }

            if (row > 0)
            {
                write(socketfd, client.loginName, sizeof(client.loginName));

                write(socketfd, client.loginPawd, sizeof(client.loginPawd));

                chatRoomFunc(socketfd);
            }
            else
            {
                printf("登录名或密码错误，请重新输入\n");
            }

            break;
        
        /* 注册 */
        case REGISTER:
            while (1)
            {
                printf("请输入你的登录名(不超过20个字符):\n");
                scanf("%s", client.loginName);
                while ((c = getchar()) != EOF && c != '\n');
                
                
                sprintf(sql, "select id from user where (id = '%s')", client.loginName);
                ret = sqlite3_get_table(chatRoomDB, sql, &result, &row, &column, &errMsg);
                if (ret != SQLITE_OK)
                {
                    printf("select error:%s\n", errMsg);
                    close(mainMenu);
                    close(socketfd);
                    exit(-1);
                }

                if (row > 0)
                {
                    printf("登录名已存在，请重新输入!\n");
                    continue;
                }

                printf("请输入你的登陆密码：\n");
                scanf("%s", client.loginPawd);
                while ((c = getchar()) != EOF && c != '\n');

                writeBytes = write(socketfd, client.loginName, sizeof(client.loginName) - 1);
                if (writeBytes < 0)
                {
                    perror("write error");
                    close(mainMenu);
                    close(socketfd);
                    exit(-1);
                }
                writeBytes = write(socketfd, client.loginPawd, sizeof(client.loginPawd) - 1);
                if (writeBytes < 0)
                {
                    perror("write error");
                    close(mainMenu);
                    close(socketfd);
                    exit(-1);
                }
                readBytes = read(socketfd, buffer, sizeof(buffer));
                if (readBytes < 0)
                {
                    perror("write error");
                    close(mainMenu);
                    close(socketfd);
                    exit(-1);
                }
                printf("%s\n", buffer);
                
                if (readBytes > 0)
                {
                    chatRoomFunc(socketfd);
                    break;
                }
            }
            
            
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
    sqlite3_close(chatRoomDB);
    close(mainMenu);
    

    return 0;
}