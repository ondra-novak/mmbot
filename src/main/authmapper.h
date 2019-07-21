/*
 * authmapper.h
 *
 *  Created on: 21. 7. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_AUTHMAPPER_H_
#define SRC_MAIN_AUTHMAPPER_H_



class AuthMapper {
public:

	AuthMapper(	std::string users, std::string realm):users(users),realm(realm) {}
	AuthMapper &operator >>= (simpleServer::HTTPHandler &&hndl) {
		handler = std::move(hndl);
		return *this;
	}

	bool checkAuth(const simpleServer::HTTPRequest &req) const {
		using namespace ondra_shared;
		if (!users.empty()) {
			auto hdr = req["Authorization"];
			auto hdr_splt = hdr.split(" ");
			StrViewA type = hdr_splt();
			StrViewA cred = hdr_splt();
			if (type != "Basic") {
				genError(req);
				return false;
			}
			auto u_splt = StrViewA(users).split(" ");
			bool found = false;
			while (!!u_splt && !found) {
				StrViewA u = u_splt();
				found = u == cred;
			}
			if (!found) {
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
	AuthMapper(	std::string users, std::string realm, simpleServer::HTTPHandler &&handler):users(users), handler(std::move(handler)) {}
	std::string users;
	std::string realm;
	simpleServer::HTTPHandler handler;
};





#endif /* SRC_MAIN_AUTHMAPPER_H_ */
