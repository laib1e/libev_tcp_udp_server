#pragma once
#include <unistd.h>

class Fd 
{
public:
	explicit Fd(int fd = -1) : fd_(fd) {}

	~Fd() 
	{
		if (fd_ >= 0) 
		{
			close(fd_);
		}
	}

	Fd(const Fd&) = delete;
	Fd& operator=(const Fd&) = delete;

	Fd(Fd&& other) noexcept 
	: fd_(other.fd_)  
	{
		other.fd_ = -1;
	}

	Fd& operator=(Fd&& other) noexcept 
	{
		if (this != &other) 
		{
			if (fd_ >= 0)
			{
				close(fd_);
			}

			fd_ = other.fd_;
			other.fd_ = -1;
		}
		return *this;
	}

	int get() const 
	{
		return fd_;
	}

	void reset(int fd = -1) noexcept
	{
		if (fd_ >= 0)
			close(fd_);
		
		fd_ = fd;
	}

private:
	int fd_ = -1;
};