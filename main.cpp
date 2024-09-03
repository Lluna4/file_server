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
#define BUFFER_SIZE 1024

int internal_epfd = 0;
int epfd = 0;

#define read_comp_pkt(size, ptr, t) const_for<size>([&](auto i){std::get<i.value>(t) = read_var<std::tuple_element_t<i.value, decltype(t)>>::call(&ptr);});

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


template<int size, typename T>
struct array_with_size
{
    T array;
    int s;

    static constexpr int getsize()
    {
        return size;
    }
};

template <typename T>
T read_type(char *v)
{
    T a;

    std::memcpy(&a, v, sizeof(T));

    return a;
}

template <int size, typename T>
array_with_size<size, T> read_array(char *v)
{
    array_with_size<size, T> a = {0};
    a.array = (T)calloc(size, sizeof(std::remove_pointer_t<T>));

    for (int i = 0; i < size; i++)
    {
        std::memcpy(&a.array[i], v, sizeof(std::remove_pointer_t<T>));
        v += sizeof(std::remove_pointer_t<T>);
    }

    return a;
}

// Template struct for reading variables
template<typename T>
concept arithmetic = std::integral<T> or std::floating_point<T>;

template<typename T>
concept IsPointer = std::is_pointer<T>::value;

template<typename T>
struct read_var
{
    static T call(char** v) requires (arithmetic<T>)
    {
        T ret = read_type<T>(*v);
        *v += sizeof(T);
        return ret;
    }
    static T call(char** v) requires (IsPointer<decltype(T::array)>)
    {
        array_with_size<T::getsize(), decltype(T::array)> arr = read_array<T::getsize(), decltype(T::array)>(*v);
        *v += (sizeof(std::remove_pointer_t<decltype(T::array)>) * T::getsize());
        return arr;
    }
};


template <typename Integer, Integer ...I, typename F> constexpr void const_for_each(std::integer_sequence<Integer, I...>, F&& func)
{
    (func(std::integral_constant<Integer, I>{}), ...);
}

template <auto N, typename F> constexpr void const_for(F&& func)
{
    if constexpr (N > 0)
        const_for_each(std::make_integer_sequence<decltype(N), N>{}, std::forward<F>(func));
}


void accept_th(int socket, int pipefd)
{
    while (true)
    {
        int clientfd = accept(socket, nullptr, nullptr);
        write(pipefd, &clientfd, sizeof(int));
    }
}

std::vector<char *> preprocess_pkts(char *buffer, int sz)
{
        std::vector<char *> ret;
        while (true)
        {
            if (*buffer == '\0')
                return ret;
            std::tuple<int, int> header;
            constexpr std::size_t size = std::tuple_size_v<decltype(header)>;
            read_comp_pkt(size, buffer, header);
            int size_ = std::get<0>(header);
            if (size_ > sz)
                return ret;
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
    char *buffer = static_cast<char *>(malloc(1024 * sizeof(char *)));
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
        events_ready_internal = epoll_wait(internal_epfd, events_internal, 1024, 10);

        for (int i = 0; i < events_ready_internal;i++)
        {
            int fd = 0;
            read(events_internal[i].data.fd, &fd, sizeof(int));
            netlib::add_to_list(fd, epfd);
            std::println("Added fd {} to epoll", fd);
            users.emplace(1, user(fd));
        }
        events_ready = epoll_wait(epfd, events, 1024, 10);
        for (int i = 0; i < events_ready;i++)
        {
            int status = recv(events[i].data.fd, buffer, 1024, 0);
            if (status == -1 || status == 0)
            {
                netlib::disconnect_server(events[i].data.fd, epfd);
                users.erase(events[i].data.fd);
                continue;
            }
            std::vector<char *> pkts = preprocess_pkts(buffer, 1024);
            if (pkts.empty())
                continue;
            for (auto pkt: pkts)
            {
                std::tuple<int, int> header;
                constexpr std::size_t size = std::tuple_size_v<decltype(header)>;

                read_comp_pkt(size, pkt, header);
                std::println("header {}, {} ", std::get<0>(header), std::get<1>(header));
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
                        send(events[i].data.fd, test.c_str(), 1024, 0);
                        break;
                    }
                    case 1:
                    {
                        std::tuple<array_with_size<1012, char *>, int> name;
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
                        std::ofstream file_write(file_->second.get_name());
                        file_write.write(pkt, std::get<0>(header));
                        file_->second.data_size += std::get<0>(header);
                        std::println("Added a {}B chunk to the file with data {}", std::get<0>(header), pkt);
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
        }
    }
    free(buffer);
    close(sockfd);
    return 0;
}
