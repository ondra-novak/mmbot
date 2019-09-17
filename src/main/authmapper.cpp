/*
 * authmapper.cpp
 *
 *  Created on: 17. 9. 2019
 *      Author: ondra
 */

#include "authmapper.h"
#include <imtjson/value.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
std::string AuthUserList::hashPwd(const std::string& user,
		const std::string& pwd) {

	unsigned char result[256];
	unsigned int result_len;
	HMAC(EVP_sha3_224(),user.data(),user.length(),
			reinterpret_cast<unsigned char *>(pwd.data()), pwd.length(),
			result,&result_len);
	std::string out;
	json::base64->encodeBinaryValue(BinaryView(result,len),[&](json::StrViewA x){
		out.append(x.data,x.length);
	});
	return out;
}

std::pair<std::string, std::string> AuthUserList::decodeBasicAuth(const json::StrViewA auth) {
	json::Value v = json::base64->decodeBinaryValue(auth);
	json::StrViewA dec = v.getString();
	auto splt = dec.split(":",2);
	json::StrViewA user = splt();
	json::StrViewA pwd = splt();
	std::string pwdhash = hashPwd(user,pwd);
	return {std::string(user), pwdhash};
}

std::vector<std::pair<std::string, std::string> > AuthUserList::decodeMultipleBasicAuth(
		const json::StrViewA auth) {
}
