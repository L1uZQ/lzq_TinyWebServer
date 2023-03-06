#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include<cstdio>
#include<sys/epoll.h>
#include<stdlib.h>
#include<unistd.h>
#include<signal.h>
#include<sys/types.h>
#include<fcntl.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<sys/stat.h>
#include<sys/mman.h>
#include<stdarg.h>
#include<errno.h>
#include"locker.h"
#include<sys/uio.h>

class http_conn{
public:
    http_conn(){}
    ~http_conn(){}

    static int m_epollfd; //所有socket上的事件都被注册到同一个epoll中
    static int m_user_count; //统计用户数量
    static const int READ_BUFFER_SIZE = 2048; //度缓冲区大小
    static const int WRITE_BUFF_SIZE = 1024; //写缓冲区大小

    //http请求方法
    enum METHOD {GET=0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS,CONNECT};

    //主状态机状态
    enum CHECK_STATE {CHECK_STATE_REQUESTLINE=0,
    CHECK_STATE_HEADER, CHECK_STATE_CONTENT};
    
    //从状态机状态
    enum LINE_STATUS {LINE_OK =0, LINE_BAD, LINE_OPEN};

    //处理http请求的可能结果
    enum HTTP_CODE {NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_REQUEST, NO_RESOURCE,
        FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION};
    

    void process(); //处理客户端的请求
    void init(int sockfd, const sockaddr_in & addr);// 初始化新接手的连接
    void close_conn(); //关闭连接
    bool read(); //非阻塞读
    bool write(); //非阻塞的写



private:
    int m_sockfd; //该http连接的socket
    sockaddr_in m_address; //通信的socket地址

    char m_read_buf[READ_BUFFER_SIZE]; //读缓冲区
    int m_read_index; //标读缓冲区中以及读入的客户端数据的最后一个字节的下一个位置

    int m_checked_index; //当前正在分析的字符在读缓冲区的位置
    int m_start_line; //当前正在解析的行的起始位置

    CHECK_STATE m_check_state; //主状态机当前状态


    void init();//初始化连接起始的信息，无参，私有
    HTTP_CODE process_read(); //解析http请求
    HTTP_CODE parse_request_line(char * text); //解析请求首行
    HTTP_CODE parse_headers(char * text); //解析请求头
    HTTP_CODE parse_content(char * text); //解析请求体

    //从状态机
    LINE_STATUS parse_line(); //获取一行
    char * get_line(){return m_read_buf + m_start_line;}
    HTTP_CODE do_request(); //具体的处理

};




#endif