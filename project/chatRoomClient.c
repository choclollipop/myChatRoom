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

#define SERVER_PORT 8080
#define SERVER_ADDR "172.30.149.120"
#define BUFFER_SIZE 300
#define BUFFER_SQL  100   

#define DEFAULT_LOGIN_NAME  21
#define DEFAULT_LOGIN_PAWD  17 

/* 建立数据库句柄 */
sqlite3 * g_chatRoomDB = NULL;



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
    F_GROUP_CHAT,
};

/* 运行状态码 */
enum STATUS_CODE
{
    ON_SUCCESS,
    ERROR = -1,
};

typedef struct clientNode
{
    char loginName[DEFAULT_LOGIN_NAME];
    char loginPawd[DEFAULT_LOGIN_PAWD];
} clientNode;


#if 0
int chatRoomFunc(int socketfd, const clientNode* client)
{
    ssize_t writeBytes = 0;
    ssize_t readBytes = 0;

    /* 登录后/注册后打开功能页面 */
    //测试
    printf("功能界面\n");

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

    /* 创建好友列表文件 */
    int friendList = open("./friendList.txt", O_RDWR | O_CREAT);
    if (friendList == -1)
    {
        perror("open funcMenu error");
        close(funcMenu);
        return ERROR;
    }
    // char friendListBuffer[]

    int choice = 0;
    int ret = 0;
    char c = '0';

    char nameBuffer[DEFAULT_LOGIN_NAME];
    bzero(nameBuffer, sizeof(nameBuffer));

    char writeBuffer[BUFFER_SIZE];
    bzero(writeBuffer, sizeof(writeBuffer));

    char sql[BUFFER_SQL];
    bzero(sql, sizeof(sql));

    /* 查询数据库所需参数 */
    char ** result = NULL;
    int row = 0;
    int column = 0;
    char * errMsg = NULL;

    while (1)
    {
        printf("%s\n", funcMenuBuffer);
        printf("请输入你需要的功能：\n");
        scanf("%d", &choice);
        while ((c = getchar()) != EOF && c != '\n');
        writeBytes = write(socketfd, &choice, sizeof(choice));
        if (writeBytes < 0)
        {
            perror("write error");
            close(funcMenu);
            close(friendList);
            return ERROR;
        }

        switch (choice)
        {
        /* 查看好友 */
        case F_FRIEND_VIEW:
            printf("全部好友:\n");
            sprintf(sql, "select * from friend");
            ret = sqlite3_get_table(clientMsgDB, sql, &result, &row, &column, &errMsg);
            if (ret != SQLITE_OK)
            {
                printf("select error : %s\n", errMsg);
                close(funcMenu);
                close(friendList);
                return ERROR;
            }
            if (row == 0)
            {
                printf("你当前没有好友！");
            }
            else
            {            
                for (int idx = 0; idx <= row; idx++)
                {
                    for (int jdx = 0; jdx < column; jdx++)
                    {
                        printf("id: %s\n", result[(idx * column) + jdx]);
                    }
                }
            }
            break;

        /* 添加好友 */
        case F_FRIEND_INCREASE:
            printf("请输入你要添加的好友id:\n");
            scanf("%s", nameBuffer);
            while ((c = getchar()) != EOF && c != '\n');

            sprintf(sql, "select id = '%s' from user", nameBuffer);
            ret = sqlite3_get_table(g_chatRoomDB, sql, &result, &row, &column, &errMsg);
            if (ret != SQLITE_OK)
            {
                printf("select error : %s\n", errMsg);
                close(funcMenu);
                close(friendList);
                return ERROR;
            }

            /* 给添加的对象发送添加请求 */
            writeBytes = write(socketfd, nameBuffer, sizeof(nameBuffer));
            if (writeBytes < 0)
            {
                perror("write error");
                close(funcMenu);
                close(friendList);
                return ERROR;
            }

            int Agree = 0;
            readBytes = read(socketfd, &Agree, sizeof(Agree));
            if (Agree == 1)
            {
                /* 同意 */
                printf("对方已同意您的请求\n");
                /* 添加到好友列表 */
                write(friendList, nameBuffer, sizeof(nameBuffer));
            }
            else if (Agree == 2)
            {
                printf("对方拒绝了您的请求\n");
            }
            else
            {
                printf("对方暂时不在线\n");
            }

            break;
        
        default:
            break;
        }
    }

    return  ON_SUCCESS;
}
#endif


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
    ret = sqlite3_open("chatRoom.db", &g_chatRoomDB);
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
            int flag = 0;
            do
            {
                printf("请输入你的登录名(不超过20个字符):\n");
                scanf("%s", client.loginName);
                while ((c = getchar()) != EOF && c != '\n');

                writeBytes = write(socketfd, client.loginName, sizeof(client.loginName));
                if (writeBytes < 0)
                {
                    perror("write error");
                    close(socketfd);
                    close(mainMenu);
                    exit(-1);
                }

                printf("请输入你的登陆密码：\n");
                scanf("%s", client.loginPawd);
                while ((c = getchar()) != EOF && c != '\n');

                writeBytes = write(socketfd, client.loginPawd, sizeof(client.loginPawd));
                if (writeBytes < 0)
                {
                    perror("write error");
                    close(socketfd);
                    close(mainMenu);
                    exit(-1);
                }

                readBytes = read(socketfd, buffer, sizeof(buffer));
                if (readBytes < 0)
                {
                    perror("read error");
                    close(socketfd);
                    close(mainMenu);
                    exit(-1);
                }

                ret = strncmp("登录成功！\n", buffer, sizeof("登录成功！\n"));
                if (ret == 0)
                {
                    /* 相等，登录成功 */
                    printf("%s", buffer);
                    flag = 0;

                    break;

                    // if (chatRoomFunc(socketfd, &client) < 0)
                    // {
                    //     break;
                    // }
                }
                else
                {
                    printf("%s", buffer);
                    flag = 1;
                }

            } while (flag);

            break;
        
        /* 注册 */
        case REGISTER:
            flag = 0;
            do
            {
                printf("请输入你的登录名(不超过20个字符):\n");
                scanf("%s", client.loginName);
                while ((c = getchar()) != EOF && c != '\n');

                writeBytes = write(socketfd, client.loginName, sizeof(client.loginName));
                if (writeBytes < 0)
                {
                    perror("write error");
                    close(mainMenu);
                    close(socketfd);
                    exit(-1);
                }

                bzero(buffer, sizeof(buffer));
                readBytes = read(socketfd, buffer, sizeof(buffer));
                if (readBytes < 0)
                {
                    perror("read error");
                    close(mainMenu);
                    close(socketfd);
                    exit(-1);
                }

                ret = strncmp("登录名已存在，请重新输入!\n", buffer, sizeof("登录名已存在，请重新输入!\n"));
                if (ret == 0)
                {
                    printf("登录名已存在，请重新输入!\n");
                    flag = 1;
                    continue;
                }
                else
                {
                    printf("%s", buffer);
                    scanf("%s", client.loginPawd);
                    while ((c = getchar()) != EOF && c != '\n');

                    writeBytes = write(socketfd, client.loginPawd, sizeof(client.loginPawd));
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
                        // chatRoomFunc(socketfd, &client);
                        break;
                    }

                    flag = 1;
                }   

            } while(flag);
            
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