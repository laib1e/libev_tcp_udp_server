#pragma once
#include <vector>
#include <deque>
#include <memory>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/udp.h>
#include <fcntl.h>

#include <stdexcept>
#include <cstring>
#include <unistd.h>

#include <FileDescriptor.hpp>

class Datagram
{
public:
	sockaddr_storage peer;
	std::vector<char> out_buffer_;
	socklen_t peer_len;

	explicit Datagram(size_t buffer_size)
	{
		out_buffer_.reserve(buffer_size);
	};
	~Datagram() = default;
};

class UDP
{
public:
	explicit UDP(uint16_t port) : port_(port) {}

	void start(struct ev_loop *loop) 
	{
		int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (sock < 0) throw std::runtime_error("SERVER INIT ERROR");
		socket_.reset(sock);
		
		std::memset(&read_watcher_, 0, sizeof(read_watcher_));
		std::memset(&write_watcher_, 0, sizeof(write_watcher_));

		struct sockaddr_in address;
		std::memset(&address, 0, sizeof(address));
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = INADDR_ANY;
		address.sin_port = htons(port_);

		{
			int opt = 1;
			int flags = fcntl(socket_.get(), F_GETFL, 0);
			setsockopt(socket_.get(), SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
			fcntl(socket_.get(), F_SETFL, flags | O_NONBLOCK);
		}

		if (bind(socket_.get(), (struct sockaddr *)&address, sizeof(address)) < 0) 
		{
			throw std::runtime_error("BIND PORT ERROR");
		}

		ev_io_init(&read_watcher_, read_client_cb, socket_.get(), EV_READ);
		ev_io_init(&write_watcher_, write_client_cb, socket_.get(), EV_WRITE);
		read_watcher_.data = this;
		write_watcher_.data = this;
		ev_io_start(loop, &read_watcher_);
	}

	void stop(struct ev_loop *loop) noexcept 
	{
		if (not loop)
			return;

		ev_io_stop(loop, &read_watcher_);
		ev_io_stop(loop, &write_watcher_);
		socket_.reset();
	}

	~UDP() = default;

	UDP(const UDP&) = delete;
	UDP& operator=(const UDP&) = delete;

	UDP(UDP&&) = delete;
	UDP& operator=(UDP&&) = delete;

private:
	Fd socket_;
	uint16_t port_;
	std::deque<Datagram> datagrams;

	static constexpr int MTU = 1400;
	struct ev_io read_watcher_;
	struct ev_io write_watcher_;

	static void read_client_cb(struct ev_loop *loop, struct ev_io *watcher, int revents) noexcept
	{
		auto *server = static_cast<UDP*>(watcher->data);
		while(true) 
		{
			char buffer[MTU];
			sockaddr_storage address{};
			socklen_t peer_addr_len = sizeof(address);
			ssize_t recv = recvfrom(watcher->fd, buffer, sizeof(buffer), 0, (struct sockaddr*)&address, &peer_addr_len);
			if (recv >= 0) 
			{
				Datagram data(MTU);
				data.peer = address;
				data.peer_len = peer_addr_len;
				data.out_buffer_.insert(data.out_buffer_.end(), buffer, buffer + recv);

				if (server->datagrams.empty()) 
				{
					ssize_t send = sendto(watcher->fd, buffer, recv, 0, (struct sockaddr*)&address, peer_addr_len);
					if (send >= 0) 
					{
						continue;
					}

					if (errno != EAGAIN and errno != EWOULDBLOCK) 
					{
						continue;
					}
				}

				server->datagrams.push_back(std::move(data));
				ev_io_start(loop, &server->write_watcher_);
				continue;
			}

			if (errno == EAGAIN || errno == EWOULDBLOCK) 
			{
				return;
			}

			return;
		}
	}

	static void write_client_cb(struct ev_loop *loop, struct ev_io *watcher, int revents) noexcept
	{
		auto *server = static_cast<UDP*>(watcher->data);
		if (server->datagrams.empty()) 
		{
			ev_io_stop(loop, &server->write_watcher_);
			return;
		}

		while (not server->datagrams.empty())
		{
			Datagram& datagram = server->datagrams.front();
			ssize_t send = sendto(watcher->fd, datagram.out_buffer_.data(), datagram.out_buffer_.size(), 0, 
							  (struct sockaddr*)&datagram.peer, datagram.peer_len);

			if (send >= 0) 
			{
				server->datagrams.pop_front();
				continue;
			} 

			if (send < 0 and (errno == EAGAIN or errno == EWOULDBLOCK)) 
			{
				return;
			}
			server->datagrams.pop_front();
		}
		ev_io_stop(loop, &server->write_watcher_);
	}
};