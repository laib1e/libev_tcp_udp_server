#pragma once
#include <vector>
#include <unordered_map>
#include <memory>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>

#include <stdexcept>
#include <cstring>
#include <unistd.h>

#include <FileDescriptor.hpp>

class ClientConnection 
{
public:
	Fd socket_;
	ev_io read_watcher_;
	ev_io write_watcher_;
	std::vector<char> out_buffer_;
	size_t out_buffer_offset_;

	explicit ClientConnection(Fd socket, size_t buffer_size) : socket_(std::move(socket)) 
	{
		std::memset(&read_watcher_, 0, sizeof(read_watcher_));
		std::memset(&write_watcher_, 0, sizeof(write_watcher_));
		out_buffer_.reserve(buffer_size);
		out_buffer_offset_ = 0;
	};
	~ClientConnection() = default;

	ClientConnection(const ClientConnection&) = delete;
	ClientConnection& operator=(const ClientConnection&) = delete;

	ClientConnection(ClientConnection&&) = delete;
	ClientConnection& operator=(ClientConnection&&) = delete;
};

class TCP
{
public:
	explicit TCP(uint16_t port) : port_(port) {}

	void start(struct ev_loop *loop) 
	{
		int sock = socket(AF_INET, SOCK_STREAM, 0);
		if (sock < 0) throw std::runtime_error("SERVER INIT ERROR");
		socket_.reset(sock);
		
		std::memset(&accept_watcher_, 0, sizeof(accept_watcher_));

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

		if (listen(socket_.get(), SOMAXCONN) < 0) 
		{
			throw std::runtime_error("LISTEN PORT ERROR");
		}

		ev_io_init(&accept_watcher_, accept_cb, socket_.get(), EV_READ);
		accept_watcher_.data = this;
		ev_io_start(loop, &accept_watcher_);
	}

	void stop(struct ev_loop *loop) noexcept 
	{
		if (not loop)
			return;

		ev_io_stop(loop, &accept_watcher_);

		for (const auto& item : clients_) 
		{
			ClientConnection& client = *item.second;
			
			ev_io_stop(loop, &client.read_watcher_);
			ev_io_stop(loop, &client.write_watcher_);
		}
		socket_.reset();
		clients_.clear();
	}

	~TCP() = default;

	TCP(const TCP&) = delete;
	TCP& operator=(const TCP&) = delete;

	TCP(TCP&&) = delete;
	TCP& operator=(TCP&&) = delete;
private:
	Fd socket_;
	uint16_t port_;
	std::unordered_map<int, std::unique_ptr<ClientConnection>> clients_;

	static constexpr int MTU = 1400;
	struct ev_io accept_watcher_;

	static void read_client_cb(struct ev_loop *loop, struct ev_io *watcher, int revents) noexcept
	{
		auto *server = static_cast<TCP*>(watcher->data);
		auto it = server->clients_.find(watcher->fd);
		if (it == server->clients_.end())
			return;

		ClientConnection& client = *it->second;

		while(true) 
		{
			char buffer[MTU];
			ssize_t n = read(client.socket_.get(), buffer, sizeof(buffer));

			if (n > 0) 
			{
				client.out_buffer_.insert(client.out_buffer_.end(), buffer, buffer + n);
				continue;
			}

			if (n < 0 and (errno == EAGAIN or errno == EWOULDBLOCK)) 
			{
				if (client.out_buffer_offset_ < client.out_buffer_.size()) 
					ev_io_start(loop, &client.write_watcher_);
				return;
			}
			ev_io_stop(loop, &client.read_watcher_);
			ev_io_stop(loop, &client.write_watcher_);
		
			server->clients_.erase(it);
			return;
		}
	}

	static void write_client_cb(struct ev_loop *loop, struct ev_io *watcher, int revents) noexcept
	{
		auto *server = static_cast<TCP*>(watcher->data);
		auto it = server->clients_.find(watcher->fd);
		if (it == server->clients_.end())
			return;

		ClientConnection& client = *it->second;

		while (client.out_buffer_offset_ < client.out_buffer_.size())
		{
			const char* data = client.out_buffer_.data() + client.out_buffer_offset_;
			size_t size = client.out_buffer_.size() - client.out_buffer_offset_;
			ssize_t n = send(client.socket_.get(), data, size, MSG_NOSIGNAL);

			if (n > 0) 
			{
				client.out_buffer_offset_ += n;
				continue;
			} else if (n < 0) {
				if (errno == EAGAIN or errno == EWOULDBLOCK) 
				{
					return;
				}

				ev_io_stop(loop, &client.read_watcher_);
				ev_io_stop(loop, &client.write_watcher_);
			
				server->clients_.erase(it);
				return;
			}
		}

		client.out_buffer_.clear();
		client.out_buffer_offset_ = 0;
		ev_io_stop(loop, &client.write_watcher_);
	}

	static void accept_cb(struct ev_loop *loop, struct ev_io *watcher, int revents) noexcept
	{
		auto *server = static_cast<TCP*>(watcher->data);

		while (true) 
		{
			struct sockaddr_in address;
			socklen_t address_len = sizeof(address);

			int socket = accept(watcher->fd, (struct sockaddr*)&address, &address_len);
			if (socket < 0) 
			{
				if (errno == EAGAIN or errno == EWOULDBLOCK)
					return;

				char msg[1024];
				int len = snprintf(msg, sizeof(msg), "error client connection: %s\n", strerror(errno));
    			::write(STDERR_FILENO, msg, len);
				return;
			}

			{
				int flags = fcntl(socket, F_GETFL, 0);
				fcntl(socket, F_SETFL, flags | O_NONBLOCK);
			}

			auto client = std::unique_ptr<ClientConnection>(new ClientConnection(Fd(socket), 4096));
			ev_io_init(&client.get()->read_watcher_, read_client_cb, client.get()->socket_.get(), EV_READ);
			ev_io_init(&client.get()->write_watcher_, write_client_cb, client.get()->socket_.get(), EV_WRITE);

			client.get()->read_watcher_.data = server;
			client.get()->write_watcher_.data = server;

			server->clients_.emplace(socket, std::move(client));
			ev_io_start(loop, &client.get()->read_watcher_);
		}
	}
};