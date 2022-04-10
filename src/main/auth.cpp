/*
 * auth.cpp
 *
 *  Created on: 4. 4. 2022
 *      Author: ondra
 */


#include "auth.h"

#include <openssl/hmac.h>
#include <random>

#include <imtjson/binary.h>
#include <userver/helpers.h>
#include <userver/header_value.h>
#include "../imtjson/src/imtjson/jwtcrypto.h"
#include "../imtjson/src/imtjson/object.h"
#include "../userver/http_client.h"
#include "../userver/http_server.h"

json::NamedEnum<ACL> strACL({
	{ACL::viewer, "viewer"},
	{ACL::reports, "reports"},
	{ACL::admin_edit, "admin_edit"},
	{ACL::admin_view, "admin_view"},
});

Auth::User Auth::get_user(const std::string_view &token) {

	auto now = std::chrono::system_clock::now();

	auto iter = std::lower_bound(tokenCache.begin(), tokenCache.end(), token, cmpTokenCache);
	if (iter != tokenCache.end() && iter->token == token && iter->exp > now) {
		return {true, iter->uname, iter->acl, iter->jwt};
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
				cacheToken(token, ut->first, ut->second.acl,false,now, now+std::chrono::hours(1));
				return {true, ut->first, ut->second.acl,false};
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
			std::optional<ACLSet> aclset;
			auto uid = p["sub"];
			auto acl = p["aud"];
			if (acl.type() == json::array) {
				aclset = acl_from_JSON(acl);
			} else if (acl.getString() == "session") {
				isjwt = false;
			}
			auto exp = std::chrono::system_clock::from_time_t(p["exp"].getUIntLong());
			if (uid.type() == json::string) {
				if (!aclset.has_value()) {
					auto ut = userMap.find(uid.getString());
					if (ut == userMap.end()) return {};
					cacheToken(token, ut->first, ut->second.acl,isjwt, now, exp);
					return {true, ut->first, ut->second.acl,isjwt};
				} else {
					cacheToken(token, uid.getString(), *aclset, isjwt,now, exp);
					return {true, uid.getString(), *aclset,isjwt};
				}
			} else {
				static std::string_view name("(default)");
				if (aclset.has_value()) {
					cacheToken(token, name, *aclset, isjwt,now, exp);
					return {true, name, *aclset,isjwt};
				} else if (isjwt) {
					cacheToken(token, name, jwt_default_acl, isjwt,now, exp);
					return {true, name, jwt_default_acl,isjwt};
				} else {
					return {};
				}

			}
		} else {
			return {};
		}
	}


}

void Auth::clear() {
}

void Auth::add_user(std::string &&uname, std::string &&hash_pwd,
		const ACLSet &acl) {
}

void Auth::set_secret(const std::string &secret) {
	session_jwt = new json::JWTCrypto_HS(secret, 256);
}

std::string Auth::generate_secret() {
	unsigned char buff[33];
	std::random_device rnd;
	for (unsigned char &c: buff) {
		c = rnd() & 0xFF;
	}
	std::string out;
	json::base64url->encodeBinaryValue(json::BinaryView(buff,sizeof(buff)), [&](std::string_view x){
		out.append(x);
	});
	return out;
}

void Auth::set_jwtsrv(json::PJWTCrypto jwtsrv, const ACLSet &default_acl) {
	this->jwtsrv = jwtsrv;
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

void Auth::cacheToken(const std::string_view &token, const std::string_view &user,
					  const ACLSet &acl, bool jwt,
					  const std::chrono::system_clock::time_point &now,
					  const std::chrono::system_clock::time_point &exp) {
	TokenInfo nwtk { std::string(token), std::string(user), acl, jwt, exp};
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

std::string Auth::create_session(const User &user, unsigned int exp_sec) {
	auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	auto exp = now + exp_sec;
	json::Value payload = json::Object {
		{"iss","mmbot"},
		{"sub", user.name},
		{"aud","session"},
		{"exp",exp},
		{"iat",now}
	};

	if (session_jwt != nullptr) {
		return json::serializeJWT(payload, session_jwt);
	} else {
		throw AdminPartyException();
	}
}


ACLSet Auth::acl_from_JSON(json::Value acl) {
	ACLSet aclset;
	for (json::Value x: acl) {
		const ACL *aitm = strACL.find(x.getString());
		if (aitm) aclset.set(*aitm);
	}
	return aclset;
}

Auth::User Auth::get_user(const std::string_view &username, const std::string_view &password) const {
	std::string pwdhash = encode_password(username, password);
	auto iter = userMap.find(username);
	if (iter == userMap.end() || iter->second.password_hash != pwdhash) return {};
	return {
		true, iter->first, iter->second.acl, false
	};
}

bool Auth::init_from_JSON(json::Value config) {
	clear();
	json::Value secret = config["session_hash"];
	if (!secret.defined()) return false;
	json::Value users = config["users"];
	if (users.empty()) return false;
	for (json::Value u: users) {
		auto name = u["uid"];
		auto password = u["pwd"];
		auto acl = u["acl"];
		add_user(name.getString(), password.getString(), acl_from_JSON(acl));
	}
	return true;
}

bool Auth::cmpTokenCache(const TokenInfo &a, const std::string_view &b) {
	return a.token < b;
}

bool AuthService::init_from_JSON(json::Value config) {
	bool r = Auth::init_from_JSON(config);
	public_acl.reset_all();
	has_public_acl = false;
	if (r) {
		if (config["guest"].getBool()) {
			has_public_acl = true;
			public_acl.set(ACL::viewer);
		}
	}
	admin_party = !r;
	return r;
}

static std::string_view findAuthCookie(std::string_view cookies) {
	while (cookies.empty()) {
		auto row = userver::splitAt(";", cookies);
		userver::trim(row);
		auto key = userver::splitAt("=", row);
		if (key == "auth") {
			userver::trim(row);
			return row;
		}
	}
	return cookies;
}

AuthService::User AuthService::get_user(const userver::HttpServerRequest  &req)  {
	if (admin_party) {
		ACLSet all;
		all.set_all();
		return {true, "admin_party", all};
	}
	auto auth_hdr = req.get("Authorization");
	std::string_view cookies = req.get("Cookie");
	auto auth_cookie = findAuthCookie(cookies);
	if (!auth_hdr.empty()) {
		User u = Auth::get_user(auth_hdr);
		if (u.exists) return u;
	}
	if (!auth_cookie.empty()) {
		User u = Auth::get_user(auth_cookie);
		if (u.exists) return u;
	}
	return {has_public_acl, "", public_acl, false};
}


void AuthService::basic_auth(userver::HttpServerRequest &req) {
	req.set("WWW-Authenticate","Basic realm=MMBOT, charset=\"UTF-8\"");
	req.sendErrorPage(401);
}

bool AuthService::check_auth(const User &user, userver::HttpServerRequest &req, bool basic_auth) const {
	if (user.exists) return true;
	if (basic_auth) {
		req.set("WWW-Authenticate","Basic realm=MMBOT, charset=\"UTF-8\"");
	} else {
		req.set("WWW-Authenticate","Bearer");
	}
	req.sendErrorPage(401);
	return false;
}

bool AuthService::check_auth(const User &user, ACL acl, userver::HttpServerRequest &req, bool basic_auth) const {
	if (!check_auth(user, req, basic_auth)) return false;
	if (user.acl.is_set(acl)) return true;
	req.sendErrorPage(403);
	return false;
}

AdminPartyException::AdminPartyException():std::runtime_error("Can't create session in 'admin_party' mode") {

}
