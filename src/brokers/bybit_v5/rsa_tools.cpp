#include "rsa_tools.h"

#include <imtjson/object.h>
#include <imtjson/string.h>
#include <chrono>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <stdexcept>
#include <memory>
#include <vector>
#include <sstream>
#include <shared/logOutput.h>

using ondra_shared::logDebug;

void openssl_error() {
    throw std::runtime_error("Openssl error");
}


using PEVP_PKEY = OSSL_Ptr<EVP_PKEY, &EVP_PKEY_free>;
using PEVP_PKEY_CTX = OSSL_Ptr<EVP_PKEY_CTX, &EVP_PKEY_CTX_free>;

using PBIO = OSSL_Ptr<BIO, &BIO_free_all>;
using PRSA = OSSL_Ptr<RSA, &RSA_free>;
using PEVP_MD_CTX = OSSL_Ptr<EVP_MD_CTX, EVP_MD_CTX_free>;


inline constexpr int recv_window = 15000;

PkPair generateKeysWithBits(int key_length) {

    PEVP_PKEY pkey(nullptr);
    PEVP_PKEY_CTX ctx ( EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL));
    if (!ctx) {
        openssl_error();
    }
    if (EVP_PKEY_keygen_init(ctx.get()) <= 0) {
        openssl_error();
    }
    if (EVP_PKEY_CTX_set_rsa_keygen_bits(ctx.get(), key_length) <= 0) {
        openssl_error();
    }

    {
        EVP_PKEY *pkey_tmp = NULL;
        /* Generate key */
        if (EVP_PKEY_keygen(ctx.get(), &pkey_tmp) <= 0) {
            openssl_error();
        }
        pkey  = PEVP_PKEY(pkey_tmp);
    }

    // Convert the private key to PKCS8 format
    PBIO pkcs8_bio ( BIO_new(BIO_s_mem()));
    PEM_write_bio_PKCS8PrivateKey(pkcs8_bio.get(), pkey.get(), NULL, NULL, 0, NULL, NULL);
    BUF_MEM* pkcs8_mem = NULL;
    BIO_get_mem_ptr(pkcs8_bio.get(), &pkcs8_mem);
    std::string priv_key_str(pkcs8_mem->data, pkcs8_mem->length);


    // Convert the public key to SPKI format
    PBIO spki_bio ( BIO_new(BIO_s_mem()));
    PEM_write_bio_PUBKEY(spki_bio.get(), pkey.get());
    BUF_MEM* spki_mem = NULL;
    BIO_get_mem_ptr(spki_bio.get(), &spki_mem);
    std::string pub_key_str(spki_mem->data, spki_mem->length);
    while (isspace(pub_key_str.back())) pub_key_str.pop_back();


    return {pub_key_str, std::move(pkey)};
}

std::string key2String(const PEVP_PKEY &pkey) {
    PBIO pkcs8_bio ( BIO_new(BIO_s_mem()));
    PEM_write_bio_PKCS8PrivateKey(pkcs8_bio.get(), pkey.get(), NULL, NULL, 0, NULL, NULL);
    BUF_MEM* pkcs8_mem = NULL;
    BIO_get_mem_ptr(pkcs8_bio.get(), &pkcs8_mem);
    std::string priv_key_str(pkcs8_mem->data, pkcs8_mem->length);
    return priv_key_str;
}

PEVP_PKEY string2key(const std::string &priv_key) {
    // Load the private key into OpenSSL's RSA struct
    PBIO key_bio (BIO_new_mem_buf(priv_key.data(), priv_key.size()));
    return PEVP_PKEY (PEM_read_bio_PrivateKey(key_bio.get(), nullptr, nullptr, nullptr));

}


json::Value genSignature(std::chrono::system_clock::time_point time_pt, std::string_view payload, std::string api_key, const PEVP_PKEY &key)
{
//    PEVP_PKEY_CTX ctx ( EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL));

    auto time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(time_pt.time_since_epoch()).count();

    std::stringstream ss;
    ss << time_ms << api_key << recv_window << payload;
    std::string param_str = ss.str();

//    logDebug("Sign: $1", param_str);

    PEVP_MD_CTX md_ctx ( EVP_MD_CTX_new() );
    const EVP_MD* md = EVP_sha256();
    if (EVP_DigestSignInit(md_ctx.get(), nullptr, md, nullptr, key.get()) != 1) {
         openssl_error();
    }
    if (EVP_DigestSignUpdate(md_ctx.get(), param_str.c_str(), param_str.size()) != 1) {
        openssl_error();
    }

    size_t signature_length;
    if (EVP_DigestSignFinal(md_ctx.get(), nullptr, &signature_length) != 1) {
         openssl_error();
    }
    std::vector<unsigned char> signature;
    signature.resize(signature_length);
    if (EVP_DigestSignFinal(md_ctx.get(), signature.data(), &signature_length) != 1) {
        openssl_error();
    }

    json::Value b64 (json::BinaryView(signature.data(),signature.size()), json::base64);

    json::Value j = json::Object{
        {"X-BAPI-API-KEY", api_key},
        {"X-BAPI-TIMESTAMP", time_ms},
        {"X-BAPI-SIGN", b64},
        {"X-BAPI-RECV-WINDOW", recv_window}
    };
//    logDebug("$1", j.toString().str());
    return j;

}
