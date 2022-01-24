/*
 * authmapper.cpp
 *
 *  Created on: 17. 9. 2019
 *      Author: ondra
 */

#include "authmapper.h"
#include <imtjson/value.h>
#include <imtjson/binary.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/pem.h>
#include <thread>

#include <imtjson/jwtcrypto.h>
#include <imtjson/object.h>
#include <simpleServer/query_parser.h>
bool AuthUserList::findUser(const std::string &user, const std::string &pwdhash) const {
	Sync _(lock);
	auto iter = users.find(user);
	return  iter != users.end() && pwdhash == iter->second;
}


void AuthUserList::setUsers(std::vector<std::pair<std::string, std::string> > &&users) {
	Sync _(lock);
	if (!users.empty()) {
		users.insert(users.end(), cfgusers.begin(), cfgusers.end());

	}
	this->users.swap(users);

}

void AuthUserList::setUser(const std::string &uname, const std::string &pwdhash) {
	Sync _(lock);
	users[uname] = pwdhash;
}

void AuthUserList::setCfgUsers(std::vector<std::pair<std::string, std::string> > &&users) {
	Sync _(lock);
	this->cfgusers.swap(users);
}

bool AuthUserList::empty() const {
	Sync _(lock);
	return users.empty();
}

std::string AuthUserList::hashPwd(const std::string_view& user,
		const std::string_view& pwd) {

	unsigned char result[256];
	unsigned int result_len;
	HMAC(EVP_sha384(),pwd.data(),pwd.length(),
			reinterpret_cast<const unsigned char *>(user.data()), user.length(),
			result,&result_len);
	std::string out;
	json::base64url->encodeBinaryValue(json::BinaryView(result,result_len),[&](const std::string_view &x){
		out.append(x);
	});
	return out;
}

AuthUserList::LoginPwd AuthUserList::decodeBasicAuth(const std::string_view &auth) {
	json::Value v = json::base64->decodeBinaryValue(auth);
	ondra_shared::StrViewA dec = v.getString();
	auto splt = dec.split(":",2);
	ondra_shared::StrViewA user = splt();
	ondra_shared::StrViewA pwd = splt();
	std::string pwdhash = hashPwd(user,pwd);
	return {std::string(user), pwdhash};
}

std::vector<AuthUserList::LoginPwd> AuthUserList::decodeMultipleBasicAuth(const std::string_view &auth) {

	std::vector<AuthUserList::LoginPwd> res;

	auto splt = ondra_shared::StrViewA(auth).split(" ");
	while (!!splt) {
		auto x = splt();
		if (!x.empty()) {
			res.push_back(decodeBasicAuth(x));
		}
	}
	return res;
}

AuthMapper::AuthMapper(	std::string realm, ondra_shared::RefCntPtr<AuthUserList> users, json::PJWTCrypto jwt, bool allow_empty):users(users),realm(realm),jwt(jwt),allow_empty(allow_empty) {}

AuthMapper &AuthMapper::operator >>= (simpleServer::HTTPHandler &&hndl) {
	handler = std::move(hndl);
	return *this;
}

AuthMapper &AuthMapper::operator >>= (simpleServer::HTTPMappedHandler &&hndl) {
	mphandler = std::move(hndl);
	return *this;
}

static StrViewA findAuthCookie(StrViewA cookie){
	auto n = cookie.indexOf("auth=");
	if (n != cookie.npos) {
		n+=5;
		auto m = cookie.indexOf(";", n);
		return cookie.substr(n, m-n);
	} else {
		return StrViewA();
	}
}

json::Value AuthMapper::checkAuth_probe(const simpleServer::HTTPRequest &req) const {
	using namespace ondra_shared;
	if (!users->empty() || (jwt != nullptr && !allow_empty)) {
		StrViewA auths[2] = {
				req["Authorization"],
				findAuthCookie(req["Cookie"])
		};

		for (StrViewA authhdr: auths) {
			if (!authhdr.empty()) {
				auto hdr_splt = authhdr.split(" ");
				StrViewA type = hdr_splt();
				StrViewA cred = hdr_splt();
				if (type == "Basic") {
					auto credobj = AuthUserList::decodeBasicAuth(cred);
					if (users->findUser(credobj.first, credobj.second)) {
						return credobj.first;
					}
				} else if (type == "Bearer" && jwt != nullptr) {
					json::Value v = json::checkJWTTime(json::parseJWT(cred, jwt));
					if (v.hasValue()) {
						return v;
					}
				} else if (cred == "" && type.indexOf(".") != type.npos) {
					return users->checkJWT(type);
				}
			}
		}
		return json::undefined;
	}
	return nullptr;;


}

bool AuthMapper::checkAuth(const simpleServer::HTTPRequest &req) const {

	if (!checkAuth_probe(req).defined()) {
		std::this_thread::sleep_for(std::chrono::seconds(1));
		genError(req,realm);
		return false;
	}
	return true;


}


void AuthMapper::operator()(const simpleServer::HTTPRequest &req) const {
	if (checkAuth(req)) {
		if (mphandler != nullptr) {
			if (!mphandler(req, req.getPath())) req.sendErrorPage(404);
			else handler(req);
		}
	}
}
bool AuthMapper::operator()(const simpleServer::HTTPRequest &req, const ondra_shared::StrViewA &vpath) const {
	if (vpath.endsWith("/set_cookie") || vpath.endsWith("/api/user")) return false;
	if (checkAuth(req)) {
		if (mphandler != nullptr) {
			return mphandler(req, vpath);
		} else {
			handler(req);
		}
	}
	return true;
}

void AuthMapper::genError(simpleServer::HTTPRequest req, const std::string &realm)  {
	req.sendResponse(simpleServer::HTTPResponse(401)
		.contentType("text/html")
		("WWW-Authenticate","Basic realm=\""+realm+"\""),
		"<html><body><h1>401 Unauthorized</h1></body></html>"
		);
}

json::PJWTCrypto AuthMapper::initJWT(const std::string &type, const std::string &pubkeyfile) {
	auto t = type.substr(0,2);
	auto b = type.substr(2);
	int bits = 0;
	json::PJWTCrypto out;
	for(char c: b) {
		if (std::isdigit(c)) bits = bits * 10 + (c - '0'); else return out;
	}
	FILE *f = fopen(pubkeyfile.c_str(),"r");
	if (f == nullptr) return out;
	if (t == "RS") {
		RSA *r = nullptr;
		if (PEM_read_RSA_PUBKEY(f,&r,0,0) == nullptr) {
			fclose(f);
			throw std::runtime_error("Error parsing RSA public key file: "+ pubkeyfile);
		}
		out = new json::JWTCrypto_RS(r, bits);
	} else if (t == "ES") {
		EC_KEY *eck = nullptr;
		if (PEM_read_EC_PUBKEY(f,&eck,0,0) == nullptr) {
			fclose(f);
			throw std::runtime_error("Error parsing ECDSA public key file: "+ pubkeyfile);
		}
		out = new json::JWTCrypto_ES(eck, bits);
	}
	fclose(f);
	return out;

}

bool AuthMapper::setCookieHandler(simpleServer::HTTPRequest req) {
	if (!req.allowMethods({"POST"})) return true;
	req.readBodyAsync(1000,[](simpleServer::HTTPRequest req){
		auto body = json::map_bin2str(req.getUserBuffer());
		simpleServer::QueryParser qp;
		StrViewA redir;
		StrViewA auth;
		StrViewA opt;
		qp.parse(body, true);
		for (auto &&v : qp) {
			if (v.first == "redir") {
				redir = v.second;
			} else if (v.first == "auth") {
				auth = v.second;
			} else if (v.first == "permanent") {
				opt = v.second == "true"?"Max-Age=34560000":"";
			}
		}
		std::string cookie = "auth=";
		if (auth == "auth") {
			auto hdr = req["Authorization"];
			if (hdr.defined()) {
				auth=hdr;
			} else {
				auth=findAuthCookie(req["Cookie"]);
			}
			if (auth.empty()) {
				return genError(req,"mmbot");
			}
		}
		if (auth.empty()) {
			opt = "Max-Age=0";
		}
		cookie.append(auth.data, auth.length);
		if (!opt.empty()) {
			cookie.append("; ");
			cookie.append(opt.data, opt.length);
		}
		simpleServer::HTTPResponse resp(redir.empty()?202:302);
		if (!redir.empty()) resp("Location", redir);
		resp("Set-Cookie", cookie);
		req.sendResponse(std::move(resp), StrViewA());
	});
	return true;

}

void AuthUserList::setJWTPwd(const std::string &pwd) {
	Sync _(lock);
	jwt = new json::JWTCrypto_HS(pwd, 256);
}

std::string AuthUserList::createJWT(const std::string &user) const {
	Sync _(lock);
	auto now = std::chrono::system_clock::now();
	return json::serializeJWT(json::Object{
		{"iat", std::chrono::system_clock::to_time_t(now)},
		{"exp", std::chrono::system_clock::to_time_t(now+std::chrono::hours(365*24))},
		{"sub", user}
	}, jwt);
}

json::Value AuthUserList::checkJWT(const std::string_view &jwt) const {
	Sync _(lock);
	json::Value resp = json::checkJWTTime(json::parseJWT(jwt, this->jwt));
	if (resp.hasValue()) {
		auto user = resp["sub"].getString();
		auto iter = users.find(user);
		return  iter != users.end()?json::Value(user):json::Value();
	} else {
		return json::Value();
	}
}
