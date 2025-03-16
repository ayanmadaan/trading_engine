#pragma once
#include <chrono>
#include <fstream>
#include <iomanip>
#include <openssl/err.h>
#include <openssl/hmac.h>
#include <openssl/ssl.h>
#include <sstream>
#include <string>

namespace helper {

uint64_t get_current_timestamp_ns() { return std::chrono::high_resolution_clock::now().time_since_epoch().count(); }

uint64_t get_current_timestamp_ms() {
    auto now = std::chrono::system_clock::now();
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    return now_ms;
}

uint64_t start_of_current_day_utc() {
    std::time_t now_tt = std::time(nullptr);
    std::tm* gmt = std::gmtime(&now_tt);
    gmt->tm_hour = 0;
    gmt->tm_min = 0;
    gmt->tm_sec = 0;
    std::time_t midnight_utc = timegm(gmt);
    return static_cast<uint64_t>(midnight_utc) * 1000ULL;
}

std::string get_current_timestamp() {
    uint64_t val = std::chrono::seconds(std::time(nullptr)).count();
    return std::to_string(val);
}

std::string getReadableTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto now_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(now);

    auto ms = now_ms.time_since_epoch().count() % 1000;

    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    auto now_tm = *std::localtime(&now_time_t);

    std::ostringstream oss;
    oss << std::put_time(&now_tm, "%Y-%m-%d||%H::%M::%S") << '.' << std::setw(3) << std::setfill('0') << ms;

    return oss.str();
}

std::string create_string_to_sign(const std::string& timestamp,
                                  const std::string& method,
                                  const std::string& request_path,
                                  const std::string& body) {
    return timestamp + method + request_path + body;
}

std::string base64_encode(const unsigned char* buffer, size_t length) {
    BIO* bio;
    BIO* b64;
    BUF_MEM* bufferPtr;

    b64 = BIO_new(BIO_f_base64());
    bio = BIO_new(BIO_s_mem());
    bio = BIO_push(b64, bio);
    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(bio, buffer, length);
    BIO_flush(bio);
    BIO_get_mem_ptr(bio, &bufferPtr);
    BIO_set_close(bio, BIO_NOCLOSE);
    BIO_free_all(bio);

    return std::string(bufferPtr->data, bufferPtr->length);
}

std::string generate_signature(const std::string& secret, const std::string& timestamp) {
    std::string method = "GET";
    std::string request_path = "/users/self/verify"; // Adjust as needed
    std::string message = timestamp + method + request_path;

    unsigned char* digest = HMAC(EVP_sha256(),
                                 secret.c_str(),
                                 secret.length(),
                                 (unsigned char*)message.c_str(),
                                 message.length(),
                                 nullptr,
                                 nullptr);
    return base64_encode(digest, SHA256_DIGEST_LENGTH);
}

std::string generate_signature_bybit(const std::string& secret, const std::string& message) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_length = 0;

    // Generate HMAC using SHA-256
    HMAC(EVP_sha256(),
         secret.c_str(),
         secret.length(),
         reinterpret_cast<const unsigned char*>(message.c_str()),
         message.length(),
         digest,
         &digest_length);

    // Convert the signature to a hex string
    std::stringstream ss;
    for(unsigned int i = 0; i < digest_length; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(digest[i]);
    }
    return ss.str();
}

void writeToFile(std::string qty, std::string price, std::string side) {
    std::ofstream file;
    file.open("/home/ubuntu/rohit/imbalance_strat/trades.txt", std::ios::app);
    if(file.is_open()) {
        file << "Trade, " << price << " Qty, " << qty << " Side, " << side << "\n";
    }
    file.close();
}

void writeToErrorFile(std::string message) {
    std::ofstream file;
    file.open("/home/ubuntu/rohit/imbalance_strat/errors.txt", std::ios::app);
    if(file.is_open()) {
        file << "Error: " << message << "\n";
    }
    file.close();
}
} // namespace helper
