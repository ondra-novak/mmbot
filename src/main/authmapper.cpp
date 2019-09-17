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

bool AuthUserList::findUser(const std::string &user, const std::string &pwdhash) const {
	Sync _(lock);
	auto iter = users.find(user);
	return  iter != users.end() && pwdhash == iter->second;
}


void AuthUserList::setUsers(std::vector<std::pair<std::string, std::string> > &&users) {
	Sync _(lock);
	users.swap(users);
}

bool AuthUserList::empty() const {
	Sync _(lock);
	return users.empty();
}

std::string AuthUserList::hashPwd(const std::string& user,
		const std::string& pwd) {

	unsigned char result[256];
	unsigned int result_len;
	HMAC(EVP_sha3_224(),user.data(),user.length(),
			reinterpret_cast<const unsigned char *>(pwd.data()), pwd.length(),
			result,&result_len);
	std::string out;
	json::base64->encodeBinaryValue(json::BinaryView(result,result_len),[&](json::StrViewA x){
		out.append(x.data,x.length);
	});
	return out;
}

AuthUserList::LoginPwd AuthUserList::decodeBasicAuth(const json::StrViewA auth) {
	json::Value v = json::base64->decodeBinaryValue(auth);
	json::StrViewA dec = v.getString();
	auto splt = dec.split(":",2);
	json::StrViewA user = splt();
	json::StrViewA pwd = splt();
	std::string pwdhash = hashPwd(user,pwd);
	return {std::string(user), pwdhash};
}

std::vector<AuthUserList::LoginPwd> AuthUserList::decodeMultipleBasicAuth(
		const json::StrViewA auth) {

	std::vector<AuthUserList::LoginPwd> res;

	auto splt = auth.split(" ");
	while (!!splt) {
		json::StrViewA x = splt();
		if (!x.empty()) {
			res.push_back(decodeBasicAuth(x));
		}
	}
	return res;
}

AuthMapper::AuthMapper(	std::string realm, ondra_shared::RefCntPtr<AuthUserList> users):users(users),realm(realm) {}
	AuthMapper &AuthMapper::operator >>= (simpleServer::HTTPHandler &&hndl) {
		handler = std::move(hndl);
		return *this;
	}

bool AuthMapper::checkAuth(const simpleServer::HTTPRequest &req) const {
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


void AuthMapper::operator()(const simpleServer::HTTPRequest &req) const {
	if (checkAuth(req)) handler(req);
}
bool AuthMapper::operator()(const simpleServer::HTTPRequest &req, const ondra_shared::StrViewA &) const {
	if (checkAuth(req)) handler(req);
	return true;
}

void AuthMapper::genError(simpleServer::HTTPRequest req) const {
	req.sendResponse(simpleServer::HTTPResponse(401)
		.contentType("text/html")
		("WWW-Authenticate","Basic realm=\""+realm+"\""),
		"<html><body><h1>401 Unauthorized</h1></body></html>"
		);
}
