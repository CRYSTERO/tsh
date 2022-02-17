#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXIT_SUCCESS    0
#define EXIT_FAILURE    1

#define TSH_BUFFER_UNIT 1024
#define TSH_TOK_UNIT    64
#define TSH_TOK_DELIM   "\t\r\n\a"

/**
 * @brief 读取用户输入
 * 
 * @return char* 读取到的命令字符串
 */
char* tsh_read_line()
{
    int     bufferSize  = TSH_BUFFER_UNIT;                          //初始buffer的大小设置为一个单元大小。之后若不够一个单元一个单元地加。
    int     position    = 0;                                        //字符串的下标
    char*   buffer      = (char*)malloc(sizeof(char) * bufferSize); //字符串
    char    ch;                                                     //用户输入的单个字符

    //检查内存是否分配成功(buffer既是字符串，又是首元素的地址，如果空间分配成功，他应该是一个非零的数，如果是零那么分配失败了)
    if(!buffer)
    {
        fprintf(stderr, "tsh [ERROR]: fail to allocate space.");
        exit(EXIT_FAILURE);
    }

    while(1)
    {
        ch = getchar(); //循环加getchar()获取字符，基操    

        //若用户输入回车或者读文件时读到了EOF标志，那么说明输入结束，将最后一位标记为'\0'，即字符串终止符，然后返回输入结果
        if(ch == EOF || ch == '\n')
        {
            buffer[position] = '\0';
            break;
        }

        buffer[position] = ch;
        position++;
        
        //若输入内容快到达buffer限度了，那么重新分配空间
        if(position >= bufferSize)
        {
            bufferSize += TSH_BUFFER_UNIT;
            buffer = realloc(buffer, bufferSize);
            if(!buffer)
            {
                fprintf(stderr, "tsh [ERROR]: fail to allocate space.");
                exit(EXIT_FAILURE);
            }
        }
    }
    return buffer;
}

/**
 * @brief 将命令字符串分隔成包含主命令和命令参数的数组
 * 
 * @param line 命令字符串
 * @return char** 去掉空格后的命令字符串数组
 */
char** tsh_split_line(char* line)
{
    int     bufferSize  = TSH_TOK_UNIT;                                 //规定初始buffer大小
    char**  tokens      = (char**)malloc(bufferSize * sizeof(char*));   //字符串数组
    char*   token;                                                      //临时存放分割下来的子串
    int     position    = 0;                                            //字符串数组中的位置

    if(!tokens)
    {
        fprintf(stderr, "tsh [ERROR]: fail to allocate space.");
        exit(EXIT_FAILURE);
    }

    /*
    * strtok(a,b)函数用法：在<string.h>头文件中，作用是将一个字符串按照给定的分隔符分割成几部分的子串(已自动加'\0')。
    * 要拿到分隔的子串需要多次调用这个函数。第一调用需要传递原字符串作为参数a，分隔符作为参数b。
    * 后续调用传递NULL作为参数a，分隔符作为参数b。函数返回char*字符串。
    */
    token = strtok(line, TSH_TOK_DELIM);
    while(token != NULL)
    {
        tokens[position] = token;
        position++;

        if(position >= bufferSize)
        {
            bufferSize += TSH_TOK_UNIT;
            tokens = (char**)realloc(tokens, bufferSize * sizeof(char*));
            if(!tokens)
            { 
                fprintf(stderr, "tsh [ERROR]: fail to allocate space.");
                exit(EXIT_FAILURE);
            }
        }
        token = strtok(NULL, TSH_TOK_DELIM);
    }
    //字符串数组用一个NULL结尾，就像在字符串中用'\0'结尾一样不可少！
    tokens[position] = NULL;
    return tokens;
}

/**
 * @brief 执行除shell自带命令之外的命令
 * 
 * @param args 命令字符串数组
 * @return int 执行结果状态码，正常返回1，异常直接甩出来
 */
int tsh_launch(char** args)
{
    pid_t   pid;
    pid_t   wpid;
    int     status;

    pid = fork();
    //fork返回0给子进程
    if(pid == 0)
    {
        /*
        代码解释：
        * 交给操作系统执行命令，拿到反馈，如果是-1表示执行失败
        * execvp(a, b)函数用于执行命令，参数a为主命令，参数b为命令的参数组。
        * 这个函数作用和exec()相同，但是多出两个：v表示vector，即接受参数组(又叫矢量vector)。
        * p表示让操作系统在他的环境变量里找到这个主命令然后执行，这样我们就无需指定该命令的具体路径。
        * 
        * 若execvp()返回了-1即表示命令执行有误(执行成功则不会返回任何值)，那么用perror打印错误信息，写上我们的程序名称这样好找错误来源。
        * 最后停止子进程并退出，shell接着跑
        */
        int flag = execvp(args[0], args);   
        if(flag == -1)
        {
            perror("tsh[ERROR]： fail to execute the command.");
        }
        exit(EXIT_FAILURE);
    }
    //fork返回负数表示进程创建失败
    else if(pid < 0)
    {
        perror("tsh[ERROR]： fail to execute the command.");
    }
    //fork返回正数给父进程
    /*
    * 代码解释：
    * waitpid(a, b, c)作用和wait()函数相同，都是让父进程等待子进程。传入的参数a若大于0表示只等待相关pid的子进程结束，
    * 而该子进程的pid在上面fork时拿到了。参数b为状态码的容器(引用传递哦，所以值会被waitpid函数改变为状态码)。
    * 参数c为可选项，WNOHANG表示子进程不结束也自行返回，而WUNTRACED表示啥我也不知道，很少用。这一参数不想写就写0.
    * 
    * 进程的状态有很多种，而我们只希望父进程在子进程运行完退出或被软件中断退出时结束等待。所以用两个宏来表示，意思就是字面意思。
    * 这两个宏给个状态码参数，若状态码符合自己的意义就返回正数。这里程序的语义就是：如果程序正常退出或中断退出，那么父进程停止等待。
    * while的逻辑有一点点绕，理清楚。
    */
    else
    {
        do
        {
            wpid = waitpid(pid, &status, WUNTRACED);
        } while (!WIFEXITED(status) && !WIFSIGNALED(status));
        
    }
    return 1;
}

/*
* 声明三个shell内置功能函数。所谓内置，就是shell自己就带着的命令，而非帮你调用其他的系统功能。
* 比如切换目录、提供帮助界面以及退出shell程序等。
* 因为tsh_help()需要遍历函数数组所以必须先声明。而后再实现函数。
*/
/**
 * @brief 切换目录
 * 
 * @param args 参数，其中args[0]应为"cd",而args[1]应为路径
 * @return int 执行结果，正常返回1
 */
int tsh_cd(char ** args);
/**
 * @brief 显示帮助界面，显示各种内置的命令
 * 
 * @param args 有没有无所谓，用不到，但是还带上防止用户瞎带参数
 * @return int 执行结果，正常返回1
 */
int tsh_help(char ** args);
/**
 * @brief 退出shell 有没有无所谓，用不到
 * 
 * @param args 有没有无所谓，用不到，但是还带上防止用户瞎带参数
 * @return int 正常返回0，这样shell的loop就停止了。
 */
int tsh_exit(char ** args);

//一个字符串数组，内含各种shell内置命令样式
char* builtin_str[] = {"cd", "help", "exit"};

//一个数组，里面放的是函数指针。注意顺序哟和上面的字符串数组对应，因为什么命令调用什么函数
int (*builtin_func[]) (char**) =
{
    &tsh_cd,
    &tsh_help,
    &tsh_exit
};

/**
 * @brief 获取shell自带命令的条数
 * 
 * @return int shell自带命令的条数
 */
int tsh_num_builtins()
{
    return sizeof(builtin_str) / sizeof(char*);
}

int tsh_cd(char ** args)
{
    //我们内置的命令，那么首先肯定要检查一下是否合规了
    if(args[1] == NULL)
    {
        fprintf(stderr, "tsh: expected path after \"cd\". \n");
    }
    else
    {
        if(chdir(args[1]) != 0)
        {
            perror("tsh: fail to change directory.");
        }
    }
    return 1;
}

int tsh_help(char ** args)
{
    printf("tsh v1.0.0 for darwin(arm64) \n");
    printf("You can type command name and hit Enter to execute it. \n");
    printf("Here are built-in commands of tsh: \n");

    //用一个循环遍历出所有的内置命令，万一以后需要增删就直接在数组中操作省的写什么switch语句
    for(int i = 0; i < tsh_num_builtins(); i++)
    {
        printf("%s \n", builtin_str[i]);
    }

    printf("If you need further info about other commands, please refer to Unix man page. \n");
    return 1;
}

int tsh_exit(char ** args)
{
    return 0;
}
int tsh_execute(char** args)
{
    //如果输入了空命令，那么无视，继续loop
    if(args[0] == NULL)
    {
        return 1;
    }

    //如果是shell自带命令，那么跳转相应置函数执行，返回结果就是本函数返回结果
    for(int i = 0; i < tsh_num_builtins(); i++)
    {
        if(strcmp(args[0], builtin_str[i]) == 0)
        {
            return (*builtin_func[i]) (args);
        }
    }

    //如果是其他命令，那么调用tsh_launch交由操作系统执行，返回结果就是本函数返回结果
    return tsh_launch(args);
}

void tsh_loop()
{
    char*   line;       //用户输入的一整条命令
    char**  args;       //用户的参数，char**表示存储字符串的数组
    int     status;     //status用以接收指令执行返回的状态码。如果1则loop将继续，如0则loop将结束。

    //这个循环表示shell的整个工作流程
    do
    {
        //每行shell开头的标识符
        printf("t$ ");
        //读入用户输入内容，从中解析出参数，并根据执行结果决定shell接下来应该怎么做
        line    =   tsh_read_line();
        args    =   tsh_split_line(line);
        status  =   tsh_execute(args);

        //命令执行结束后回收空间
        free(line);
        free(args);
    } while (status);
}

int main(int argc, char const *argv[])
{
    tsh_loop();
    return EXIT_SUCCESS;
}