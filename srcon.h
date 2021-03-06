//MODIFIED LIBRARY FROM
//https://github.com/Phil25/SourceRCON

#ifndef SRCON_H
#define SRCON_H

#include <iostream>
#include <string.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <chrono>
#include <thread>

#include <QObject>

#include <string>


#define SERVERDATA_AUTH 3
#define SERVERDATA_EXECCOMMAND 2
#define SERVERDATA_AUTH_RESPONSE 2
#define SERVERDATA_RESPONSE_VALUE 0

#define SRCON_DEFAULT_TIMEOUT 4
#define SRCON_HEADER_SIZE 14
#define SRCON_SLEEP_THRESHOLD 1024
#define SRCON_SLEEP_MILLISECONDS 500

struct srcon_addr{
    std::string addr;
    int port;
    std::string pass;
};

class srcon : public QObject {
    Q_OBJECT
    const srcon_addr addr;
    const int sockfd;
    unsigned int id;
    bool connected;

public:
    srcon(const srcon_addr addr, const int timeout=SRCON_DEFAULT_TIMEOUT);
    srcon(const std::string address, const int port, const std::string password, const int timeout=SRCON_DEFAULT_TIMEOUT);
    virtual ~srcon();

    std::string send(const std::string message, int type=SERVERDATA_EXECCOMMAND);

    inline bool get_connected() const{
        return connected;
    }

    inline bool is_connected() const{
        return get_connected();
    }

    inline srcon_addr get_addr() const{
        return addr;
    }

private:
    bool connect(int timeout=SRCON_DEFAULT_TIMEOUT) const;
    std::string recv(unsigned long) const;
    size_t read_packet_len() const;
    void pack(unsigned char packet[], std::string data, int packet_len, int id, int type) const;
    unsigned char* read_packet(unsigned int&, bool&) const;
    size_t byte32_to_int(unsigned char*) const;
};

//#define DEBUG 1
#ifdef DEBUG
#define LOG(str) std::clog << str << std::endl;
#else
#define LOG(str)
#endif

#endif
