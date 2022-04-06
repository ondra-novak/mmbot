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
#include <unordered_map>

#include <imtjson/value.h>
#include <imtjson/jwt.h>

class Auth {
public:

	struct ACL {
		///true, if user exists
		bool exists = false;
		///true, if user is viewer
		bool viewer = false;
		///true, if user can see reports
		bool reports = false;
		///true, if user can view settings
		bool admin_view = false;
		///true, if user can edit settings
		bool admin_edit = false;
	};


	ACL getUserACL(const std::string &token) ;

	///clear all settings
	void clear();
	///add user to db
	/**
	 * @param uname username (ident)
	 * @param hash_pwd password - use empty if user can't use password login
	 * @param acl user's acl
	 */
	void add_user(std::string &&uname, std::string &&hash_pwd, const ACL &acl);
	///set session secret
	void set_secret(const std::string &secret);
	///generate session secret
	static std::string generate_secret();
	///set jwt server to allow login server autentification
	/**
	 * @param jwtsrv token initialized with public key
	 * @param default_acl default ACL for non-existing users
	 */
	void set_jwtsrv(json::PJWTCrypto jwtsrv, const ACL &default_acl);

	static std::string encode_password(const std::string_view& user, const std::string_view& pwd);

protected:


	struct UserInfo {
		std::string password_hash;
		ACL acl;
	};

	using UserMap = std::unordered_map<std::string, UserInfo>;

	struct TokenInfo {
		std::string token;
		std::string uname;
		std::chrono::system_clock::time_point exp;
	};

	using TokenCache = std::vector<TokenInfo>;

	static bool cmpTokenCache(const TokenInfo &a, const TokenInfo &b);

	void cacheToken(const std::string_view &token, const std::string_view &user, const std::chrono::system_clock::time_point &now, const std::chrono::system_clock::time_point &exp);

	UserMap userMap;
	TokenCache tokenCache, cache_tmp;
	ACL non_auth_acl;
	ACL jwt_default_acl;
	json::PJWTCrypto jwtsrv;
	json::PJWTCrypto session_jwt;

};



#endif /* SRC_MAIN_AUTH_H_ */
