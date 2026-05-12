extern "C" 
{
	#include <ev.h>
}

#include <TCP.hpp>
#include <UDP.hpp>

template<typename Protocol>
class Server
{
public:
	explicit Server(uint16_t port) : loop_(EV_DEFAULT), proto_(port)
	{
		proto_.start(loop_);
	}

	void run() 
	{
		ev_run(loop_, 0);
	}

	void stop() noexcept
	{
		if (not loop_) 
			return;
		
		proto_.stop(loop_);
		ev_break(loop_, EVBREAK_ALL);
		loop_ = nullptr;
	}

	~Server() 
	{
		stop();
	}

	Server(const Server&) = delete;
	Server& operator=(const Server&) = delete;

	Server(Server&&) = delete;
	Server& operator=(Server&&) = delete;

private:
	struct ev_loop* loop_ = nullptr;
	Protocol proto_;
};

int main() 
{
	auto serv = std::unique_ptr<Server<UDP>>(new Server<UDP>(0));
	serv->run();
}