/*
 * abstractExtern.h
 *
 *  Created on: 27. 5. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_ABSTRACTEXTERN_H_
#define SRC_MAIN_ABSTRACTEXTERN_H_
#include <mutex>

#include <imtjson/string.h>
#include <imtjson/value.h>
#include <string>

#include "../shared/handle.h"
#include "../shared/logOutput.h"
class AbstractExtern {
public:

	AbstractExtern (const std::string_view & workingDir, const std::string_view & name, const std::string_view & cmdline, int timeout);
	~AbstractExtern ();

	bool preload();
	virtual void onConnect() {}
	void stop();

	///Send request
	/**
	 * @param name command name
	 * @param args arguments
	 * @param idle set true if the command is called during idle, so it is not tread as action
	 * @return result value
	 */
	json::Value jsonRequestExchange(json::String name, json::Value args);

	class Exception: public std::exception {
	public:
		Exception(std::string &&msg, const std::string &name, const std::string &command);
		const std::string& getCommand() const {	return command;}
		const std::string& getMsg() const {	return msg;}
		const std::string& getName() const { return name;}
		virtual const char *what() const noexcept override;

	protected:
		const std::string whatmsg;
		const std::string msg;
		const std::string name;
		const std::string command;
	};


protected:

	static const int invval;
	static void handleClose(int fd);

	using FD = ondra_shared::Handle<int, void(*)(int), &handleClose, &invval>;
	struct Pipe {
		FD read;
		FD write;
	};


	FD extin;
	FD extout;
	FD exterr;
	pid_t chldid = -1;
	std::string name;
	std::string cmdline;
	std::string workingDir;
	ondra_shared::LogObject log;
	int timeout;

	mutable std::recursive_mutex lock;
	using Sync = std::unique_lock<std::recursive_mutex>;

	class Reader;

	void spawn();
	void kill();

	static Pipe makePipe();
	int msgCntr = 1;
	bool binary_mode = false;

	json::Value jsonExchange(json::Value request);
	static bool writeJSON(json::Value v, FD &fd, bool binary_mode, int timeout);
//	static json::Value readJSON(FD &fd, int timeout);
	static bool writeString(std::string_view ss, int timeout, FD &fd);
};



#endif /* SRC_MAIN_ABSTRACTEXTERN_H_ */
