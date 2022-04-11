/*
 * auth.h
 *
 *  Created on: 4. 4. 2022
 *      Author: ondra
 */

#ifndef SRC_MAIN_AUTH_H_
#define SRC_MAIN_AUTH_H_
#include <string>
#include <chrono>
#include <shared/linear_map.h>

#include <imtjson/value.h>
#include <imtjson/jwt.h>
#include <imtjson/namedEnum.h>
#include <shared/shared_object.h>

namespace userver {
	class HttpServerRequest;
}

enum class ACL {
	viewer=1,
	reports=2,
	admin_view=3,
	admin_edit=4
};

extern json::NamedEnum<ACL> strACL;

struct ACLSet {
	unsigned int val = 0;
	bool is_set (const ACL &x) const {return (val & (1<<static_cast<unsigned int>(x)));}
	void set (const ACL &x) { val = (val | (1<<static_cast<unsigned int>(x)));}
	void reset (const ACL &x) {val = (val & ~(1<<static_cast<unsigned int>(x)));}
	void toggle(const ACL &x, bool val) {
		if (val) set(x); else reset(x);
	}
	bool toggle(const ACL &x) {
		bool z = !is_set(x);
		if (z) set(x); else reset(x);
		return z;
	}
	void set_all() {val = ~0;}
	void reset_all() {val = 0;}
};


class Auth {
public:

	struct User {
		bool exists = false;
		std::string_view name;
		ACLSet acl;
		bool jwt;
	};


	User get_user(const std::string_view &token) ;

	User get_user(const std::string_view &username, const std::string_view &password) const;

	///clear all settings
	void clear();
	///add user to db
	/**
	 * @param uname username (ident)
	 * @param hash_pwd password - use empty if user can't use password login
	 * @param acl user's acl
	 */
	void add_user(std::string &&uname, std::string &&hash_pwd, const ACLSet &acl);
	///set session secret
	void set_secret(const std::string &secret);
	///generate session secret
	static std::string generate_secret();
	///set jwt server to allow login server autentification
	/**
	 * @param jwtsrv token initialized with public key
	 * @param default_acl default ACL for non-existing users
	 */
	void set_jwtsrv(json::PJWTCrypto jwtsrv, const ACLSet &default_acl);

	static std::string encode_password(const std::string_view& user, const std::string_view& pwd);

	std::string create_session(const User &user, unsigned int exp_sec);


	static ACLSet acl_from_JSON(json::Value acl);

	bool init_from_JSON(json::Value config);


protected:


	struct UserInfo {
		std::string password_hash;
		ACLSet acl;
		bool jwt;
	};

	using UserMap = ondra_shared::linear_map<std::string, UserInfo, std::less<> >;

	struct TokenInfo {

		std::string token;
		std::string uname;
		ACLSet acl;
		bool jwt;
		std::chrono::system_clock::time_point exp;
	};

	using TokenCache = std::vector<TokenInfo>;

	static bool cmpTokenCache(const TokenInfo &a, const std::string_view &b);

	void cacheToken(const std::string_view &token, const std::string_view &user, const ACLSet &acl, bool jwt, const std::chrono::system_clock::time_point &now, const std::chrono::system_clock::time_point &exp);

	UserMap userMap;
	TokenCache tokenCache, cache_tmp;
	ACLSet jwt_default_acl;
	json::PJWTCrypto jwtsrv;
	json::PJWTCrypto session_jwt;

};


class AuthService: public Auth {
public:

	using Auth::get_user;
	User get_user(const userver::HttpServerRequest &req) ;
	bool init_from_JSON(json::Value config);
	bool check_auth(const User &user, userver::HttpServerRequest &req, bool basic_auth = false) const;
	bool check_auth(const User &user, ACL acl, userver::HttpServerRequest &req, bool basic_auth = false) const;
	static void basic_auth(userver::HttpServerRequest &req);


	static json::Value conv_pwd_to_hash(json::Value users);

protected:

	bool admin_party = true;
	bool has_public_acl = false;
	ACLSet public_acl;

};

class AdminPartyException: public std::runtime_error {
public:
	AdminPartyException();
};

using PAuthService = ondra_shared::SharedObject<AuthService>;


#endif /* SRC_MAIN_AUTH_H_ */
