/*
 * abstractExtern.cpp
 *
 *  Created on: 27. 5. 2019
 *      Author: ondra
 */




#include "abstractExtern.h"

#include <unistd.h>
#include <string_view>
#include <signal.h>
#include <fcntl.h>
#include <imtjson/parser.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <experimental/filesystem>

#include <cstring>
#include <sstream>
#include <thread>

#include <imtjson/binjson.tcc>

#include "../shared/linux_waitpid.h"
#include "istockapi.h"

const int AbstractExtern::invval = -1;

AbstractExtern::AbstractExtern(const std::string_view & workingDir, const std::string_view & name, const std::string_view & cmdline)
:name(name), cmdline(cmdline),workingDir(workingDir),log(name) {
}


template<typename Fn>
void parseArguments(std::string_view args, Fn &&result) {

	std::string buffer;
	bool escaped = false;
	bool quoted = false;
	bool wasquoted = false;
	for (char c: args) {
		if (escaped) {
			buffer.push_back(c);
			escaped = false;
		} else {
			if (!quoted && isspace(c)) {
				if (wasquoted || !buffer.empty()) {
					result(buffer);
					buffer.clear();
					wasquoted = false;
				}
			} else if (c == '"') {
				quoted = !quoted;
				wasquoted = true;
			} else if (c == '\\') {
				escaped = true;
			} else {
				buffer.push_back(c);
			}
		}
	}
	if (wasquoted || !buffer.empty()) {
		result(buffer);
	}
}

template<typename Fn>
void parseArgumentList(std::string_view args, Fn &&result) {
	std::vector<std::string> data;
	parseArguments(args,[&](const std::string &arg) {
		data.emplace_back(arg);
	});
	std::vector<char *> ptrs;
	for (auto &&x:data) {
		ptrs.push_back(const_cast<char *>(x.c_str()));
	}
	ptrs.push_back(nullptr);
	result(ptrs.data());
}


static void report_error(const char *desc) {
	int e = errno;
	std::ostringstream buff;
	buff << "System exception: " << e << " " << strerror(e) << " while '" << desc << '"';
	throw std::runtime_error(buff.str());
}

static void report_timeout(const char *desc) {
	std::ostringstream buff;
	buff << "TIMEOUT while '" << desc << '"';
	throw std::runtime_error(buff.str());

}


AbstractExtern::Pipe AbstractExtern::makePipe() {
	int tmp[2];
	int r = pipe2(tmp, O_CLOEXEC);

	if (r < 0) report_error("pipe2");
	return {FD(tmp[0]), FD(tmp[1])};
}




void AbstractExtern::spawn() {
	Sync _(lock);
	using ondra_shared::Handle;

	int status;
	//collect any zombie
	waitpid(-1,&status,WNOHANG);

	{

		log.progress("Connecting to broker: cmdline='$1', workdir='$2'", cmdline, workingDir);

		Pipe proc_input (makePipe());
		Pipe proc_output (makePipe());
		Pipe proc_error (makePipe());
		Pipe proc_control (makePipe());

		parseArgumentList(cmdline, [&](char * const arglist[]) {

			pid_t frk = fork();
			if (frk == -1) {
				int err = errno;
				throw std::runtime_error(strerror(err));
			}
			if (frk == 0) {

				try {
					std::experimental::filesystem::current_path(workingDir);
					if (dup2(proc_input.read, 0)<0) report_error("dup->stdin");
					if (dup2(proc_output.write, 1)<0) report_error("dup->stdout");
					if (dup2(proc_error.write, 2)<0) report_error("dup->stderr");
					execvp(arglist[0], arglist);
					report_error("execlp");

				} catch (std::exception &e) {

					const char *w = e.what();
					if (write(proc_control.write, w, strlen(w))<0) exit(0);
				}
				exit(0);

			} else {

				proc_control.write.close();
				std::string errmsg;
				{
					char buff[256];
					auto r = read(proc_control.read, buff, sizeof(buff));
					while (r > 0) {
						errmsg.append(buff,r);
						r = read(proc_control.read, buff, sizeof(buff));
					}
				}
				if (!errmsg.empty()) throw std::runtime_error(errmsg);
				extout = std::move(proc_output.read);
				exterr = std::move(proc_error.read);
				extin = std::move(proc_input.write);
				chldid = frk;
				houseKeepingCounter = 0;
			}
		});
	}

	onConnect();


}

void AbstractExtern::handleClose(int fd) {
	::close(fd);
}


void AbstractExtern::kill() {
	Sync _(lock);
	if (chldid != -1) {

		ondra_shared::WaitPid wpid(chldid);
		extin.close();
		if (!wpid.wait_for(std::chrono::seconds(3))) {
			::kill(chldid, SIGTERM);
			if (!wpid.wait_for(std::chrono::seconds(10))) {
				::kill(chldid, SIGKILL);
				if (!wpid.wait_for(std::chrono::seconds(10))) {
					log.error("Unable to terminate broker! (TIMEOUT waiting on SIGKILL)");
				}
			}
		}
		int status = wpid.getExitCode();
		if (WIFSIGNALED(status)) {
			log.note("Broker process disconnected because signal: $1", WTERMSIG(status));
		} else {
			log.note("Broker process disconnected. Exit code : $1", WEXITSTATUS(status));
		}
		chldid = -1;
	}
}

AbstractExtern::~AbstractExtern() {
	kill();
}

static void waitForRead(int fd) {
	struct pollfd fds = {fd, POLLIN|POLLHUP,0};
	int r = poll(&fds, 1, 30000);
	if (r != 1) throw std::runtime_error("Broker read timeout");
}
static void waitForWrite(int fd) {
	struct pollfd fds = {fd, POLLOUT,0};
	int r = poll(&fds, 1, 30000);
	if (r != 1) throw std::runtime_error("Broker write timeout");
}


class AbstractExtern::Reader {
public:
	Reader (FD &fd):fd(fd) {}
	std::string_view read() {
		if (buff.empty()) {
			return readBuff();
		}
		else {
			auto x = buff;
			buff = std::string_view();
			return x;
		}
	}
	void putback(std::string_view data) {
		buff = data;
	}

	int operator()() {
		auto d = read();
		if (d.empty()) return -1;
		char z = d[0];
		putback(d.substr(1));
		return z;
	}

protected:
	std::string_view buff;
	char data[1000];
	FD &fd;

	std::string_view readBuff() {
		waitForRead(fd);
		int i = ::read(fd, data, sizeof(data));
		if (i < 1) return std::string_view();
		else return std::string_view(data, i);
	}
};


bool AbstractExtern::writeJSON(json::Value v, FD& fd) {
	auto s = v.stringify();
	s = s + "\n";
	std::string_view ss(s.c_str(),s.length());
	while (!ss.empty()) {
		waitForWrite(fd);
		int i = write(fd, ss.data(), ss.length());
		if (i < 1) {
			return false;
		}
		ss = ss.substr(i);
	}
	return true;
}

void AbstractExtern::housekeeping(int counter) {
	Sync _(lock);
	if (chldid != -1) {
		houseKeepingCounter++;
		if (houseKeepingCounter >= counter) {
			log.progress("Stopping the idle broker");
			stop();
		} else {
			log.debug("HouseKeepCounter: $1", houseKeepingCounter);
		}
	}
}

json::Value AbstractExtern::readJSON(FD& fd) {
	return json::Value::parse(Reader(fd));

}


void AbstractExtern::preload() {
	Sync _(lock);
	if (chldid == -1) {
		spawn();
	}
}

void AbstractExtern::stop() {
	kill();
}

json::Value AbstractExtern::jsonExchange(json::Value request) {
	Sync _(lock);

	houseKeepingCounter=0;

	std::string z;
	std::string lastStdErr;

	if (chldid == -1) {
		spawn();
	}
	bool verbose = log.isLogLevelEnabled(ondra_shared::LogLevel::debug);
	if (verbose) log.debug("SEND: $1", request.toString());
	if (writeJSON(request, extin) == false) {
		kill();
	}
	do {
		try {
			struct pollfd fds[2];
			fds[0].fd = extout;
			fds[0].events = POLLIN;
			fds[0].revents = 0;
			fds[1].fd = exterr;
			fds[1].events = POLLIN;
			fds[1].revents = 0;
			int r = poll(fds,2,30000);
			if (r == 0) report_timeout("poll");
			if (r < 0) report_error("poll");
			if (fds[1].revents) {
				Reader errrd(exterr);
				bool rep;
				do {
					lastStdErr = std::move(z);
					do {
						auto buff = errrd.read();
						if (buff.empty()) {
							throw std::runtime_error(json::String({
								"Connection to API lost: ",
								request.toString()," - err: ",
									lastStdErr.empty()?"N/A": lastStdErr.c_str()}).c_str());
						}
						auto pos = buff.find('\n');
						if (pos == buff.npos) z.append(buff);
						else {
							z.append(buff.substr(0,pos));
							errrd.putback(buff.substr(pos+1));
							rep = pos+1 < buff.length();
							log.note("stderr: $1",  z);
							break;
						}
					} while (true);
				}while (rep);
			}
			if (fds[0].revents) {
					auto ret = readJSON(extout);
					if (verbose) log.debug("RECV: $1", ret.toString());
					return ret;

			}
		} catch (...) {
			kill();
			throw;
		}
	}
	while (true);
}

json::Value AbstractExtern::jsonRequestExchange(json::String name, json::Value args) {
	Sync _(lock);
	auto resp = jsonExchange({name, args});
	if (resp[0].getBool() == true) {
		auto result = resp[1];
		return result;
	} else {
		auto error = resp[1];
		throw IStockApi::Exception(error.toString().str());
	}
}

