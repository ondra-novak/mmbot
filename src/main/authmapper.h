/*
 * authmapper.h
 *
 *  Created on: 21. 7. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_AUTHMAPPER_H_
#define SRC_MAIN_AUTHMAPPER_H_
#include <shared/refcnt.h>
#include <imtjson/stringview.h>
#include <mutex>
#include <string>
#include <vector>

#include "../server/src/simpleServer/http_parser.h"
#include "../shared/linear_map.h"

class AuthUserList: public ondra_shared::RefCntObj {
public:
	using Sync = std::unique_lock<std::recursive_mutex>;
	using UserMap = ondra_shared::linear_map<std::string, std::string>;
	using LoginPwd = UserMap::value_type;

	bool findUser(const std::string &user, const std::string &pwdhash) const;

	static std::string hashPwd(const std::string &user, const std::string &pwd);
	static LoginPwd decodeBasicAuth(const json::StrViewA auth);
	static std::vector<LoginPwd> decodeMultipleBasicAuth(const json::StrViewA auth);

	void setUsers(std::vector<std::pair<std::string, std::string> > &&users);
	void setCfgUsers(std::vector<std::pair<std::string, std::string> > &&users);
	bool empty() const;
	void setUser(const std::string &uname, const std::string &pwdhash);

protected:
	mutable std::recursive_mutex lock;
	//standard user table
	UserMap users;
	//user table from config
	UserMap cfgusers;
};


class AuthMapper {
public:

	AuthMapper(	std::string realm, ondra_shared::RefCntPtr<AuthUserList> users);
	AuthMapper &operator >>= (simpleServer::HTTPHandler &&hndl);
	bool checkAuth(const simpleServer::HTTPRequest &req) const;
	void operator()(const simpleServer::HTTPRequest &req) const;
	bool operator()(const simpleServer::HTTPRequest &req, const ondra_shared::StrViewA &) const;
	void genError(simpleServer::HTTPRequest req) const;
	ondra_shared::RefCntPtr<AuthUserList> getUsers() const {return users;}

protected:
//	AuthMapper(	std::string users, std::string realm, simpleServer::HTTPHandler &&handler):users(users), handler(std::move(handler)) {}
	ondra_shared::RefCntPtr<AuthUserList> users;
	std::string realm;
	simpleServer::HTTPHandler handler;
};





#endif /* SRC_MAIN_AUTHMAPPER_H_ */
