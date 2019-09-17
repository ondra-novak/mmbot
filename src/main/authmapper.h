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

	bool findUser(const std::string &user, const std::string &pwdhash) const {
		Sync _(lock);
		auto iter = users.find(user);
		return  iter != users.end() && pwdhash == iter->second;
	}

	static std::string hashPwd(const std::string &user, const std::string &pwd);
	static std::pair<std::string,std::string> decodeBasicAuth(const json::StrViewA auth);
	static std::vector<std::pair<std::string,std::string> > decodeMultipleBasicAuth(const json::StrViewA auth);

	void setUsers(std::vector<std::pair<std::string, std::string> > &users) {
		Sync _(lock);
		users.swap(users);
	}

	bool empty() const {
		Sync _(lock);
		return users.empty();
	}

protected:
	mutable std::recursive_mutex lock;
	ondra_shared::linear_map<std::string, std::string> users;
};


class AuthMapper {
public:

	AuthMapper(	std::string realm, ondra_shared::RefCntPtr<AuthUserList> users):users(users),realm(realm) {}
	AuthMapper &operator >>= (simpleServer::HTTPHandler &&hndl) {
		handler = std::move(hndl);
		return *this;
	}

	bool checkAuth(const simpleServer::HTTPRequest &req) const {
		using namespace ondra_shared;
		if (!users->empty()) {
			auto hdr = req["Authorization"];
			auto hdr_splt = hdr.split(" ");
			StrViewA type = hdr_splt();
			StrViewA cred = hdr_splt();
			if (type != "Basic") {
				genError(req);
				return false;
			}
			auto credobj = AuthUserList::decodeBasicAuth(cred);
			if (!users->findUser(credobj.first, credobj.second)) {
				genError(req);
				return false;
			}
		}
		return true;
	}


	void operator()(const simpleServer::HTTPRequest &req) const {
		if (checkAuth(req)) handler(req);
	}
	bool operator()(const simpleServer::HTTPRequest &req, const ondra_shared::StrViewA &) const {
		if (checkAuth(req)) handler(req);
		return true;
	}

	void genError(simpleServer::HTTPRequest req) const {
		req.sendResponse(simpleServer::HTTPResponse(401)
			.contentType("text/html")
			("WWW-Authenticate","Basic realm=\""+realm+"\""),
			"<html><body><h1>401 Unauthorized</h1></body></html>"
			);
	}

protected:
//	AuthMapper(	std::string users, std::string realm, simpleServer::HTTPHandler &&handler):users(users), handler(std::move(handler)) {}
	ondra_shared::RefCntPtr<AuthUserList> users;
	std::string realm;
	simpleServer::HTTPHandler handler;
};





#endif /* SRC_MAIN_AUTHMAPPER_H_ */
