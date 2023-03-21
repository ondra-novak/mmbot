/*
 * rsa_tools.h
 *
 *  Created on: 21. 3. 2023
 *      Author: ondra
 */

#ifndef SRC_BROKERS_BYBIT_V5_RSA_TOOLS_H_
#define SRC_BROKERS_BYBIT_V5_RSA_TOOLS_H_
#include <chrono>
#include <memory>
#include <string>
#include <imtjson/value.h>

extern "C" {
    typedef struct evp_pkey_st EVP_PKEY;
    void EVP_PKEY_free(EVP_PKEY *pkey);
}

template<typename T, void(*ptr)(T *)>
struct OSSL_SmartPtr {
    void operator()(T *x) {ptr(x);}
    using Type = std::unique_ptr<T, OSSL_SmartPtr<T, ptr> >;
};

template<typename T, void(*ptr)(T *)>
using OSSL_Ptr = typename OSSL_SmartPtr<T, ptr>::Type;

using PEVP_PKEY = OSSL_Ptr<EVP_PKEY, &EVP_PKEY_free>;


struct PkPair {
    std::string public_key;
    PEVP_PKEY private_key;
};

PkPair generateKeysWithBits(int key_length);
std::string key2String(const PEVP_PKEY &key);
PEVP_PKEY string2key(const std::string &key);
json::Value genSignature(std::chrono::system_clock::time_point time_pt, std::string_view payload, std::string api_key, const PEVP_PKEY &key);

#endif /* SRC_BROKERS_BYBIT_V5_RSA_TOOLS_H_ */
