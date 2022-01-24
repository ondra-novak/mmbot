/*
 * authmapper.h
 *
 *  Created on: 21. 7. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_AUTHMAPPER_H_
#define SRC_MAIN_AUTHMAPPER_H_
#include <shared/refcnt.h>
#include <imtjson/jwt.h>
#include <mutex>
#include <string>
#include <vector>

#include <simpleServer/http_parser.h>
#include <shared/linear_map.h>
#include <simpleServer/http_pathmapper.h>

class AuthUserList: public ondra_shared::RefCntObj {
public:
	using Sync = std::unique_lock<std::recursive_mutex>;
	using UserMap = ondra_shared::linear_map<std::string, std::string>;
	using LoginPwd = UserMap::value_type;

	bool findUser(const std::string &user, const std::string &pwdhash) const;

	static std::string hashPwd(const std::string_view &user, const std::string_view &pwd);
	static LoginPwd decodeBasicAuth(const std::string_view &auth);
	static std::vector<LoginPwd> decodeMultipleBasicAuth(const std::string_view &auth);

	void setUsers(std::vector<std::pair<std::string, std::string> > &&users);
	void setCfgUsers(std::vector<std::pair<std::string, std::string> > &&users);
	bool empty() const;
	void setUser(const std::string &uname, const std::string &pwdhash);
	void setJWTPwd(const std::string &pwd);
	std::string createJWT(const std::string &user) const;
	json::Value checkJWT(const std::string_view &jwt) const;

protected:
	mutable std::recursive_mutex lock;
	//standard user table
	UserMap users;
	//user table from config
	UserMap cfgusers;

	json::PJWTCrypto jwt;
};


class AuthMapper {
public:

	AuthMapper(	std::string realm, ondra_shared::RefCntPtr<AuthUserList> users, json::PJWTCrypto jwt, bool allow_empty);
	AuthMapper &operator >>= (simpleServer::HTTPHandler &&hndl);
	AuthMapper &operator >>= (simpleServer::HTTPMappedHandler &&hndl);
	bool checkAuth(const simpleServer::HTTPRequest &req) const;
	json::Value checkAuth_probe(const simpleServer::HTTPRequest &req) const;
	void operator()(const simpleServer::HTTPRequest &req) const;
	bool operator()(const simpleServer::HTTPRequest &req, const ondra_shared::StrViewA &) const;
	static void genError(simpleServer::HTTPRequest req, const std::string &realm) ;
	ondra_shared::RefCntPtr<AuthUserList> getUsers() const {return users;}
	void genError(simpleServer::HTTPRequest req) const {genError(req, realm);}

	static json::PJWTCrypto initJWT(const std::string &type, const std::string &pubkeyfile);
	static bool setCookieHandler(simpleServer::HTTPRequest req);

protected:
//	AuthMapper(	std::string users, std::string realm, simpleServer::HTTPHandler &&handler):users(users), handler(std::move(handler)) {}
	ondra_shared::RefCntPtr<AuthUserList> users;
	std::string realm;
	simpleServer::HTTPHandler handler;
	simpleServer::HTTPMappedHandler mphandler;
	json::PJWTCrypto jwt;
	bool allow_empty;
};





#endif /* SRC_MAIN_AUTHMAPPER_H_ */
