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

	AbstractExtern (const std::string_view & workingDir, const std::string_view & name, const std::string_view & cmdline);
	~AbstractExtern ();

	bool preload();
	virtual void onConnect() {}
	void stop();
	void housekeeping(int counter);

	///Send request
	/**
	 * @param name command name
	 * @param args arguments
	 * @param idle set true if the command is called during idle, so it is not tread as action
	 * @return result value
	 */
	json::Value jsonRequestExchange(json::String name, json::Value args, bool idle = false);

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

	mutable std::recursive_mutex lock;
	using Sync = std::unique_lock<std::recursive_mutex>;

	class Reader;

	void spawn();
	void kill();

	static Pipe makePipe();
	int msgCntr = 1;
	int houseKeepingCounter = 0;


	json::Value jsonExchange(json::Value request, bool idle);
	static bool writeJSON(json::Value v, FD &fd);
	static json::Value readJSON(FD &fd);

};



#endif /* SRC_MAIN_ABSTRACTEXTERN_H_ */
