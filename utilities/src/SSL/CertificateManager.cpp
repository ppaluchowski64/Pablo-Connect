#include <SSL/CertificateManager.h>

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>
#include <openssl/err.h>
#include <DebugLog.h>
#include <stdio.h>
#include <filesystem>

void CertificateManager::GenerateCertificate(const std::filesystem::path& path) {
    const std::string keyPath = (path / "privateKey.key").string();
    const std::string certPath = (path / "certificate.crt").string();

    std::filesystem::create_directories(path);

    const unsigned char *C_value = reinterpret_cast<const unsigned char *>("PL");
    const unsigned char* O_value = reinterpret_cast<const unsigned char*>("PabloConnect");
    const unsigned char* CN_value = reinterpret_cast<const unsigned char*>("localhost");
    constexpr int exp  = 60 * 60 * 24 * 30;

    EVP_PKEY* pkey = nullptr;
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr);
    if (!ctx || EVP_PKEY_keygen_init(ctx) <= 0 || EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx, NID_X9_62_prime256v1) <= 0 || EVP_PKEY_keygen(ctx, &pkey) <= 0) {
        const std::string error = GetOpenSSLError();
        Debug::LogError("Failed to generate EC key (" + error + ")");
        EVP_PKEY_CTX_free(ctx);
        return;
    }

    EVP_PKEY_CTX_free(ctx);

    FILE* keyfile = fopen(keyPath.c_str(), "wb");
    if (!keyfile) {
        Debug::LogError("Failed to open keyfile");
        return;
    }
    PEM_write_PrivateKey(keyfile, pkey, nullptr, nullptr, 0, nullptr, nullptr);
    fclose(keyfile);

    X509* cert = X509_new();
    ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
    X509_gmtime_adj(X509_get_notBefore(cert), 0);
    X509_gmtime_adj(X509_get_notAfter(cert), exp);
    X509_set_version(cert, 2);
    X509_set_pubkey(cert, pkey);

    X509_NAME* name = X509_get_subject_name(cert);
    X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC, C_value, -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC, O_value, -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, CN_value, -1, -1, 0);
    X509_set_issuer_name(cert, name);

    if (!X509_sign(cert, pkey, EVP_sha256())) {
        const std::string error = GetOpenSSLError();
        Debug::LogError("Failed to sign certificate (" + error + ")");
        return;
    }

    FILE* certfile = fopen(certPath.c_str(), "wb");
    if (!certfile) {
        const std::string error = GetOpenSSLError();
        Debug::LogError("Failed to open cert file (" + error + ")");
        return;
    }
    PEM_write_X509(certfile, cert);
    fclose(certfile);

    X509_free(cert);
    EVP_PKEY_free(pkey);
}

bool CertificateManager::IsCertificateValid(const std::filesystem::path &path) {
    constexpr int certificateMinimalTimeLeft = 60 * 10;

    const std::string certPath = (path / "certificate.crt").string();
    FILE* fp = fopen(certPath.c_str(), "r");
    if (!fp) {
        return false;
    }

    X509* cert = PEM_read_X509(fp, nullptr, nullptr, nullptr);
    fclose(fp);

    if (!cert) {
        return false;
    }

    time_t now = time(nullptr);
    time_t future = now + certificateMinimalTimeLeft;

    const bool valid = (X509_cmp_time(X509_get_notBefore(cert), &now) <= 0 &&
                        X509_cmp_time(X509_get_notAfter(cert), &future) >= 0);

    X509_free(cert);
    return valid;
}


std::string CertificateManager::GetOpenSSLError() {
    const unsigned long err = ERR_get_error();
    if (err == 0)
        return "Unknown OpenSSL error (no error on queue)";
    char errBuf[256];
    ERR_error_string_n(err, errBuf, sizeof(errBuf));
    return std::string(errBuf);
}
