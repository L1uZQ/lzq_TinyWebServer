#include"http_conn.h"
#include<sys/epoll.h>

//初始化静态变量
int http_conn::m_epollfd = -1;
int http_conn::m_user_count = 0;

// 定义HTTP响应的一些状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";



//网站资源目录
const char * doc_root ="/home/lzq/lzq_TinyWebServer/resources";

//设置文件描述符非阻塞
void setnonblocking(int fd){
    int old_flag = fcntl(fd, F_GETFL);
    int new_flag = old_flag | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_flag);
}

//用来向epoll中添加需要监听的文件描述符
void addfd(int epollfd, int fd, bool one_shot){
    epoll_event event;
    event.data.fd = fd;
    //event.events = EPOLLIN | EPOLLRDHUP;
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;

    if(one_shot){
        event.events | EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    //设置文件描述符非阻塞
    setnonblocking(fd);
}

//从epoll中删除文件描述符
void removefd(int epollfd, int fd){
    epoll_ctl(epollfd, EPOLL_CTL_DEL,fd,0);
    close(fd);
}

//修改文件描述符,重置socket上的EPOLLONESHOT 事件，确保下一次可读时，事件能被触发
void modfd(int epollfd, int fd, int ev){
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

//初始化连接
void http_conn::init(int sockfd, const sockaddr_in & addr){

    m_sockfd = sockfd;
    m_address = addr;

    //设置端口复用
    int reuse = 1;
    setsockopt(m_sockfd,SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    //添加到epoll对象中
    addfd(m_epollfd, sockfd, true);
    m_user_count++; //总用户数加一   

    init(); //把变量初始化一下 
 }

void http_conn::init(){
    m_check_state = CHECK_STATE_REQUESTLINE; //初始化状态解析请求首行
    m_checked_index = 0; //初始化为0
    m_start_line = 0;
    m_read_index = 0;

    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_linker=0;
    m_content_length=0;

    bzero(m_read_buf, READ_BUFFER_SIZE);//置为0

}


//关闭连接
void http_conn::close_conn(){
    if(m_sockfd!=-1){
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--; //关闭一个连接，客户总数量减一
    }
}

//循环读取客户数据，直到无数据可读或者对方关闭连接
bool http_conn::read(){

    if(m_read_index >= READ_BUFFER_SIZE){
        return false;
    }

    //读取到的字节
    int bytes_read=0;
    while(true){
        bytes_read = recv(m_sockfd, m_read_buf + m_read_index, READ_BUFFER_SIZE-m_read_index, 0);
        if(bytes_read == -1){
            if(errno == EAGAIN || errno == EWOULDBLOCK){
                //没有数据
                break;
            }
            return false;
        }else if(bytes_read == 0){
            //对方关闭连接
            return false;
        }

        m_read_index += bytes_read;
    }
   // printf("一次性读出数据\n");
    printf("读取到了数据: %s\n",m_read_buf);
    return true;
}


//主状态机
http_conn::HTTP_CODE http_conn::process_read(){

    //初始状态
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;

    char * text = 0;
    
    //一行一行去解析
    while(((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK)) || ((line_status = parse_line())== LINE_OK)){
        //获取一行数据
        text= get_line();
        m_start_line = m_checked_index;
        printf("got 1 http line : %s\n", text);

        switch(m_check_state){ //根据主状态机的情况做不同的处理
            case CHECK_STATE_REQUESTLINE: //请求行
            {
                ret = parse_request_line(text);
                if(ret == BAD_REQUEST){
                    return BAD_REQUEST; //请求语法错误，直接结束
                }
                break;
            }

            case CHECK_STATE_HEADER:
            {
                ret = parse_headers(text);
                if(ret == BAD_REQUEST){
                    return BAD_REQUEST;
                }else if(ret == GET_REQUEST){ //获取一个完整的请求头
                    return do_request(); //执行 （解析具体请求内容的函数）
                }
            }

            case CHECK_STATE_CONTENT:
            {
                ret = parse_content(text);
                if(ret == GET_REQUEST){
                    return do_request();
                }
                line_status = LINE_OPEN;//如果失败，行数据还不完整
                break;

            }

            default :
            {
                return INTERNAL_ERROR;
            }
        }
        return NO_REQUEST; //如果整个都没有出错，请求不完整
    }

    return NO_REQUEST;
}

//  解析http请求行， 获得请求方法，目标URL，HTTP版本
http_conn::HTTP_CODE http_conn::parse_request_line(char * text){
    // GET / HTTP/1.1
    m_url = strpbrk(text, " \t"); //返回空格的地方
    
    *m_url++ = '\0';   //先把m_url变成'\0'

    char * method = text;

    if(strcasecmp(method, "GET")==0){
        m_method=GET;
    }else{
        return BAD_REQUEST; //语法错误的请求,暂时没有实现其它请求
    }

    m_version = strpbrk(m_url, " \t");
    if(!m_version){
        return BAD_REQUEST;
    }
    *m_version ++ ='\0';
    if(strcasecmp(m_version, "HTTP/1.1")!=0){
        return BAD_REQUEST;
    }
    if(strncasecmp(m_url, "http://",7)==0){
        m_url += 7; //把http://跳过
        m_url = strchr(m_url, '/'); //取第一个/的位置
        // 192.168.1.1:9006/index.html
    }

    if(!m_url || m_url[0]!='/'){
        return BAD_REQUEST;
    }

    m_check_state = CHECK_STATE_HEADER; //主状态机状态从首行变成检查请求头

    return NO_REQUEST;
}


//解析HTTP请求的一个头部消息
http_conn::HTTP_CODE http_conn::parse_headers(char * text){
    //遇到空行，表示头部解析完毕
    if(text[0]== '\0'){
        //如果HTTP请求有消息体，则还需要读取m_content_length字节的消息体
        //状态转移
        if(m_content_length != 0){
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        //请求完整
        return GET_REQUEST;
    } else if(strncasecmp(text, "Connection:",11)==0){
        text += 11;
        text += strspn(text, " \t");
        if(strcasecmp(text,"Keep-alive")==0){
            m_linker = true;
        }
    }else if(strncasecmp(text, "Content-Length:",15)==0){
            //处理Content-Length头部字段
            text +=15;
            text += strspn(text, " \t");
            m_content_length = atol(text);

    } else if(strncasecmp(text, "Host:", 5)==0){
            //处理Host头部字段
            text += 5;
            text += strspn(text, " \t");
            m_host = text;
    }else{
            printf("unknow header\n");
    }
    return NO_REQUEST;
}


//没有真正解析请求体，只是判断是否被完整读入
http_conn::HTTP_CODE http_conn::parse_content(char * text){
    if(m_read_index >= (m_content_length + m_checked_index)){
        text[m_content_length] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

//从状态机，解析一行，依据是\r\n
http_conn::LINE_STATUS http_conn::parse_line(){
    char temp;
    //遍历一行数据
    for( ; m_checked_index < m_read_index; ++m_checked_index){
        temp = m_read_buf[m_checked_index];
        if(temp == '\r'){
            if(m_checked_index+1 == m_read_index){
                return LINE_OPEN; //没有读取到完整的
            }else if(m_read_buf[m_checked_index+1]=='\n'){
                m_read_buf[m_checked_index++] = '\0';
                m_read_buf[m_checked_index++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }else if(temp == '\n'){
            if((m_checked_index>1) && (m_read_buf[m_checked_index-1] == '\r')){
                m_read_buf[m_checked_index-1]= '\0';
                m_read_buf[m_checked_index++]= '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        return LINE_OPEN; //都不满足，数据不完整，return LINE_OPEN
    }

    return LINE_OK;
}

//得到一个完整的HTTP请求时，分析目标文件的属性
//目标文件存在，就使用mmap将其映射到内存地址m_file_address处，
//并告诉调用者获取文件成功
http_conn::HTTP_CODE http_conn::do_request(){
    //"/home"
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    strncpy(m_real_file + len, m_url, FILENAME_LEN -len -1);

    //获取m_real_file文件的相关状态信息，-1失败，0成功
    if(stat(m_real_file, &m_file_stat)<0){
        return NO_RESOURCE;
    }

    //判断访问权限
    if(!(m_file_stat.st_mode & S_IROTH)){
        return FORBIDDEN_REQUEST;
    }

    //判断是否为目录
    if(S_ISDIR(m_file_stat.st_mode)){
        return BAD_REQUEST;
    }

    //以只读方式打开文件
    int fd = open(m_real_file, O_RDONLY);
    //创建内存映射,把网页的数据映射到m_file_address，将来发送给客户端
    m_file_address = (char*)mmap(0,m_file_stat.st_size, PROT_READ, MAP_PRIVATE,fd, 0);
    close(fd);
    return FILE_REQUEST;

}

// 对内存映射区执行munmap操作
void http_conn::unmap() {
    if( m_file_address )
    {
        munmap( m_file_address, m_file_stat.st_size );
        m_file_address = 0;
    }
}



bool http_conn::write(){
    printf("一次性写完数据\n");
    return true;
}

//由线程池中的工作线程调用，处理http请求的入口函数
void http_conn::process(){
    //解析http请求

    HTTP_CODE read_ret = process_read();
    
    //重新检测一下
    if(read_ret == NO_REQUEST){
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return ;
    }

    //printf("parse request, create response\n");

    //生成响应
    bool write_ret = process_write(read_ret);
    


}
