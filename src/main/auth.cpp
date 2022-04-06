/*
 * auth.cpp
 *
 *  Created on: 4. 4. 2022
 *      Author: ondra
 */


#include "auth.h"

#include <openssl/hmac.h>
#include <imtjson/binary.h>
#include <userver/helpers.h>
#include <userver/header_value.h>
Auth::ACL Auth::getUserACL(const std::string &token) {

	auto now = std::chrono::system_clock::now();

	auto iter = std::lower_bound(tokenCache.begin(), tokenCache.end(), TokenInfo{token, std::string(), now}, cmpTokenCache);
	if (iter != tokenCache.end() && iter->token == token && iter->exp > now) {
		auto ut = userMap.find(iter->uname);
		if (ut == userMap.end()) return {};
		return ut->second.acl;
	} else {
		std::string_view t = token;
		auto type = userver::splitAt(" ", t);
		if (userver::HeaderValue::iequal(type, "basic")) {
			json::Value d = json::base64->decodeBinaryValue(t);
			std::string astr = json::map_bin2str(d.getBinary(json::base64));
			std::string_view password = astr;
			std::string username = userver::splitAt(":", username);
			std::string pwhash = encode_password(username, password);
			auto ut = userMap.find(username);
			if (ut == userMap.end()) return {};
			if (ut->second.password_hash == pwhash) {
				cacheToken(token, ut->first, now, now+std::chrono::hours(1));
				return ut->second.acl;
			} else {
				return {};
			}
		} else if (userver::HeaderValue::iequal(type, "bearer")) {
			json::Value p = json::parseJWT(t, session_jwt);
			bool isjwt = false;
			if (!p.hasValue() && jwtsrv != nullptr) {
				p = json::parseJWT(t, jwtsrv);
				isjwt = true;
			}
			if (!p.hasValue()) {
				return {};
			}
			if (!json::checkJWTTime(p, now).hasValue()) {
				return {};
			}
			auto uid = p["uid"];
			if (uid.type() == json::string) {
				auto ut = userMap.find(uid.getString());
				if (ut == userMap.end()) return {};
				cacheToken(token, ut->first, now, std::chrono::system_clock::from_time_t(p["exp"].getUIntLong()));
				return ut->second.acl;
			} else if (isjwt) {
				return jwt_default_acl;
			} else {
				return {};
			}
		} else {
			return {};
		}
	}


}

void Auth::clear() {
}

void Auth::add_user(std::string &&uname, std::string &&hash_pwd,
		const ACL &acl) {
}

void Auth::set_secret(const std::string &secret) {
}

std::string Auth::generate_secret() {
}

void Auth::set_jwtsrv(json::PJWTCrypto jwtsrv, const ACL &default_acl) {

}


std::string Auth::encode_password(const std::string_view& user, const std::string_view& pwd) {

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

void Auth::cacheToken(const std::string_view &token, const std::string_view &user, const std::chrono::system_clock::time_point &now, const std::chrono::system_clock::time_point &exp) {
	TokenInfo nwtk { std::string(token), std::string(user), exp};
	cache_tmp.clear();
	TokenCache::iterator iter = tokenCache.begin();
	TokenCache::iterator end = tokenCache.end();
	while (iter != end) {
		if (iter->token > nwtk.token) {
			cache_tmp.push_back(std::move(nwtk));
			nwtk.token.clear();
		}
		if (iter->exp > now) {
			cache_tmp.push_back(std::move(*iter));
		}
		++iter;
	}
	if (!nwtk.token.empty()) {
		cache_tmp.push_back(std::move(nwtk));
	}
	std::swap(tokenCache, cache_tmp);
}
