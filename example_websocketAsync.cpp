#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/strand.hpp>
#include <iostream>
#include <memory>
#include <thread>
#include <string>

#include <unistd.h>
#include <sys/syscall.h> 

#ifndef SYS_gettid
#error "SYS_gettid unavailable on this system"
#endif

#define gettid() ((pid_t)syscall(SYS_gettid))

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

class EchoWebSocket: public std::enable_shared_from_this<EchoWebSocket>
{
    websocket::stream<beast::tcp_stream> ws;
    beast::flat_buffer buffer;

public:
    EchoWebSocket(tcp::socket&& socket):ws(std::move(socket)){}
    void run(){
        ws.async_accept([self{shared_from_this()}](beast::error_code ec){
            
            if(ec){std::cout<<ec.message()<<std::endl; return;}
            self->echo();
        });
    }
    void echo(){
        ws.async_read(buffer,[self{shared_from_this()}](beast::error_code ec,std::size_t bytes_transferred){
            
            std::cout<<"thread id "<<gettid()<<std::endl;

            if(ec == websocket::error::closed) return;
            if(ec){std::cout<<ec.message()<<std::endl; return;}
            auto out = beast::buffers_to_string(self->buffer.cdata());
            std::cout<<out<<std::endl;

            self->ws.async_write(self->buffer.data(),[self](beast::error_code ec, std::size_t bytes_transferred){
                
                if(ec){std::cout<<ec.message()<<std::endl; return;}
                self->buffer.consume(self->buffer.size());    
                
                self->echo();//If we dont call this, this program echo only once.
            }); 
        });
    }
};

class Listener: public std::enable_shared_from_this<Listener>
{
    net::io_context& ioc;
    tcp::acceptor acceptor;
public:
    //make_strand prevents race condition when there is more thread for ioc. strand = sequential invocation of event handlers
    Listener(net::io_context& ioc,unsigned short int port):
        ioc(ioc),
        acceptor(net::make_strand(ioc),{net::ip::make_address("127.0.0.1"),port}){};
    void asyncAccept(){
        //If we dont use shared_from_this here, it gives segmentation fault. Need a ownership, else it recursively call this method.
        acceptor.async_accept(net::make_strand(ioc),[self{shared_from_this()}](boost::system::error_code ec,tcp::socket socket){
            std::make_shared<EchoWebSocket>(std::move(socket))->run();
            std::cout<<"connection accepted"<<std::endl;
            self->asyncAccept();
        });
    }
};

int main(int argc, char** argv){

    auto const port = 8083;
    int threads = 4;
    net::io_context ioc{threads};
    std::make_shared<Listener>(ioc,port)->asyncAccept();

    std::vector<std::thread> v;
    v.reserve(threads-1);
    for(auto i=threads-1; i>0;i--){
        v.emplace_back([&ioc](){
            ioc.run();
        });
    }

    ioc.run();
    return 0;
}