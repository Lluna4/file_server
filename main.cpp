#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <print>
#include <fcntl.h>
#include <sys/epoll.h>
#include <thread>
#include <cstring>
#include <tuple>
#include <filesystem>
#include <fstream>
#include <vector>
#include <type_traits>
#include <map>
#include <iostream>
#include "netlib.h"
#include "comp_time_read.hpp"
#define BUFFER_SIZE 4096

int internal_epfd = 0;
int epfd = 0;


enum STATUS
{
    START,
    FILE_NAME_GIVEN,
    FILE_SENT
};

class user
{
    public:
        explicit user(int sockfd)
            :sockfd_(sockfd)
        {
            status = START;
        }

        void set_state(int state)
        {
            status = state;
        }

        int get_state()
        {
            return status;
        }
    private:
        int sockfd_;
        int status;
};

class file
{
    public:
        explicit file(std::string name)
            :name_(name)
        {
            size = 0;
            data_size = 0;
        }
        int data_size;
        std::string get_name()
        {
            return name_;
        }
        void set_size(int size_)
        {
            size = size_;
        }
        int get_size()
        {
            return size;
        }

    private:
        std::string name_;
        int size;
};

std::map<int, user> users;
std::map<std::string, file> files;
std::map<int, file> files_uploading;



void accept_th(int socket, int pipefd)
{
    while (true)
    {
        int clientfd = accept(socket, nullptr, nullptr);
        netlib::add_to_list(clientfd, epfd);
        std::println("Added fd {} to epoll", clientfd);
        users.emplace(1, user(clientfd));
    }
}

std::vector<char *> preprocess_pkts(char *buffer, int sz, int sock)
{
        std::vector<char *> ret;
        while (true)
        {
            if (sz <= 0)
                return ret;
            if (*buffer == '\0')
                return ret;
            std::tuple<int, int> header;
            constexpr std::size_t size = std::tuple_size_v<decltype(header)>;
            read_comp_pkt(size, buffer, header);
            std::println("header {}, {} ", std::get<0>(header), std::get<1>(header));
            if (std::get<0>(header) <= 0 || std::get<0>(header) > BUFFER_SIZE)
            {
                std::println("Invalid packet!!");
                return ret;
            }
            int size_ = std::get<0>(header);
            if (size_ > sz)
            {
                /*char *buf = (char *)calloc(BUFFER_SIZE, sizeof(char));
                memcpy(buf, buffer, sz);
                char *start_buf = buf;
                buf += sz;
                int status = recv(sock, buf, BUFFER_SIZE - sz, 0);
                std::println("status {} {}", status, sz);
                buffer = start_buf;*/
                return ret;
            }
            buffer -= 8;
            char *new_str = (char *)calloc(size_ + ((sizeof(int) * 2) + 1), sizeof(char));
            std::memcpy(new_str, buffer, size_ + 8);
            ret.push_back(new_str);
            buffer += size_ + 8;
            sz -= size_ + 8;
        }
}

int main()
{
    int pipefds[2];
    int sockfd = netlib::init_server("127.0.0.1", 8000);
    if (sockfd == -1)
        return -1;
    char *buffer = static_cast<char *>(malloc(BUFFER_SIZE * sizeof(char *)));
    internal_epfd = epoll_create1(0);
    epfd = epoll_create1(0);
    pipe(pipefds);
    std::thread a(accept_th, sockfd, pipefds[1]);
    a.detach();
    netlib::add_to_list(pipefds[0], internal_epfd);

    int events_ready_internal = 0;
    epoll_event events_internal[1024];
    int events_ready = 0;
    epoll_event events[1024];

    std::println("Ready");
    while (true)
    {
        events_ready = epoll_wait(epfd, events, 1024, -1);
        for (int i = 0; i < events_ready;i++)
        {
            int status = recv(events[i].data.fd, buffer, BUFFER_SIZE, 0);
            if (status < BUFFER_SIZE)
            {
                status += recv(events[i].data.fd, &buffer[status], BUFFER_SIZE - status, 0);
            }
            //std::println("Status {} {}", status, buffer[0]);
            if (status == -1 || status == 0)
            {
                netlib::disconnect_server(events[i].data.fd, epfd);
                users.erase(events[i].data.fd);
                continue;
            }
            std::vector<char *> pkts = preprocess_pkts(buffer, status - 8, events[i].data.fd);
            if (pkts.empty())
            {
                std::println("Empty!");
                continue;
            }
            for (auto pkt: pkts)
            {
                std::tuple<int, int> header;
                constexpr std::size_t size = std::tuple_size_v<decltype(header)>;

                read_comp_pkt(size, pkt, header);
                
                std::string test;
                switch (std::get<1>(header))
                {
                    case -1:
                    {
                        netlib::disconnect_server(events[i].data.fd, epfd);
                        users.erase(events[i].data.fd);
                        break;
                    }
                    case 0:
                    {
                        for (auto& value: files)
                        {
                            test.append(value.second.get_name());
                            test.push_back(';');
                        }
                        std::println("{}", test);
                        send(events[i].data.fd, test.c_str(), BUFFER_SIZE, 0);
                        break;
                    }
                    case 1:
                    {
                        std::tuple<array_with_size<BUFFER_SIZE, char *>, int> name;
                        constexpr std::size_t size_ = std::tuple_size_v<decltype(name)>;
                        read_comp_pkt(size_, pkt, name);
                        std::println("Received file name {} and size {}", std::get<0>(name).array, std::get<1>(name));
                        auto user_ = users.find(events[i].data.fd);
                        user found_user(events[i].data.fd);
                        if (user_ != users.end())
                        {
                            found_user = user_->second;
                            found_user.set_state(FILE_NAME_GIVEN);
                        }
                        int fd = events[i].data.fd;
                        auto result = files_uploading.insert({fd, file(std::get<0>(name).array)});
                        result.first->second.set_size(std::get<1>(name));
                        break;
                    }
                    case 2:
                    {
                        auto file_ = files_uploading.find(events[i].data.fd);
                        if (file_ == files_uploading.end())
                            break;
                        std::ofstream file_write(file_->second.get_name(), std::ios_base::app);
                        file_write.write(pkt, std::get<0>(header));
                        file_->second.data_size += std::get<0>(header);
                        //std::println("Added a {}B chunk to the file with data {}", std::get<0>(header), pkt);
                        break;
                    }
                    case 3:
                        auto file_ = files_uploading.find(events[i].data.fd);
                        if (file_ == files_uploading.end())
                            break;
                        files.insert({file_->second.get_name(), file_->second});
                        std::println("Created file {} with size {}B", file_->second.get_name(), file_->second.data_size);;
                        files_uploading.erase(events[i].data.fd);
                        break;
                }
                
            }
            memset(buffer, 0, BUFFER_SIZE);
        }
    }
    free(buffer);
    close(sockfd);
    return 0;
}
