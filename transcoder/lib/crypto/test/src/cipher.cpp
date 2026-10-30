#include <gtest/gtest.h>

#include "utpl/crypto/manager.h"

using namespace utpl;

TEST(lib_license_crypto, aes_256_gcm_simple) {
    static const std::string plaintext = "\
    Lorem ipsum dolor sit amet, \
    consectetur adipiscing elit. \
    Sed do eiusmod tempor incididunt \
    ut labore et dolore magna \
    aliqua. Ut enim ad minim \
    veniam, quis nostrud exercitation \
    ullamco laboris nisi ut aliquip ex \
    ea commodo consequat. Duis aute irure \
    dolor in reprehenderit in voluptate velit \
    esse cillum dolore eu fugiat nulla pariatur. \
    Excepteur sint occaecat cupidatat non proident, \
    sunt in culpa qui officia deserunt mollit \
    anim id est laborum.";

    static const std::vector<uint8_t> key = {
        0x87, 0x1e, 0xfd, 0x07, 0x8c, 0x37, 0x66, 0xdb, 0xf7, 0x93, 0x65, 0x6a,
        0x33, 0x26, 0x1c, 0x6a, 0xa9, 0x46, 0xb5, 0x3c, 0xf2, 0x8d, 0x94, 0x78,
        0x96, 0x0a, 0xd6, 0x6e, 0xd8, 0x80, 0xbf, 0xff
    };

    // openssl rand 32 > /tmp/key.bin
    // cat /tmp/key.txt | xxd -i
    // https://docs.openssl.org/1.1.1/man1/enc/#supported-ciphers 
    // AEAD not supported by openssl enc
    // echo -n "Hello, World!" | openssl enc -aes-256-gcm -nosalt -K $(cat /tmp/key.bin | xxd -p -c 32) -iv 0 -p
    {
        std::vector<uint8_t> enc;
        std::vector<uint8_t> dec;

        std::vector<uint8_t> iv;

        auto &crypto = crypto::getCryptoManager();
        
        auto crypter = crypto.newCipher(crypto::CipherAlgorithm::AES, crypto::CipherMode::GCM);
        ASSERT_TRUE(crypter);

        auto k = crypto.loadKey(key);
        ASSERT_TRUE(k);

        {
            {
                crypter->init(true, *k);

                iv  = crypter->getIv();
                enc = crypter->final(plaintext.data(), plaintext.size());
            }
            
            {
                crypto::CipherIvParameters params(iv);

                crypter->init(false, *k, &params);

                dec = crypter->final(enc);
            }

            ASSERT_EQ(std::string(dec.begin(), dec.end()), plaintext);
        }

        {
            {
                crypter->init(true, *k);
                crypter->updateAAD({ 0x01, 0x02, 0x03, 0x04 });

                iv  = crypter->getIv();
                enc = crypter->final(plaintext.data(), plaintext.size());
            }
            
            {
                crypto::CipherIvParameters params(iv);

                crypter->init(false, *k, &params);
                crypter->updateAAD({ 0x01, 0x02, 0x03, 0x04 });

                dec = crypter->final(enc);
            }

            ASSERT_EQ(std::string(dec.begin(), dec.end()), plaintext);
        }

        {
            {
                crypter->init(true, *k);

                iv  = crypter->getIv();
                enc = crypter->update(plaintext.data(), plaintext.size());
                
                auto f = crypter->final();
                enc.insert(enc.end(), f.begin(), f.end());
            }

            {
                crypto::CipherIvParameters params(iv);

                crypter->init(false, *k, &params);
                
                dec = crypter->update(enc);

                auto f = crypter->final();
                dec.insert(dec.end(), f.begin(), f.end());
            }

            ASSERT_EQ(std::string(dec.begin(), dec.end()), plaintext);
        }
    }
}


TEST(lib_license_crypto, aes_gcm_aad_ext) {
    // Generated using AesGcmTest.java in sandbox directory
    const uint8_t ivBytes[]  = { 0x69, 0xfa, 0x01, 0xfb, 0xa0, 0xcc, 0x40, 0x54, 0x99, 0x22, 0xd3, 0x25 };
    const uint8_t aadBytes[] = { 0x25, 0x19, 0x4d, 0x39, 0x1b, 0x64, 0x08 };
    const uint8_t keyBytes[] = {
        0x5d, 0xb2, 0x34, 0xf3, 0x1b, 0x5a, 0x03, 0x4b, 0xb1, 0xfd, 0x11, 0x07,
        0x6e, 0x2e, 0xef, 0xd3, 0x26, 0xa9, 0x5e, 0x4d, 0x06, 0xc8, 0x2c, 0x04,
        0x47, 0xba, 0x14, 0x40, 0x06, 0xe4, 0xb3, 0xd9
    };

    const uint8_t dataBytes[] = {
        0x77, 0x18, 0x3d, 0x52, 0xdc, 0xed, 0x98, 0x51, 0xf1, 0x72, 0x7f, 0xdc,
        0x75, 0xa5, 0x34, 0xa6, 0x86, 0xc6, 0x89, 0x21, 0xda, 0xa2, 0x13, 0x93,
        0xd9, 0x00, 0x67, 0xf0, 0x92, 0x17, 0xee, 0xa5, 0x79, 0x18, 0x3a, 0x7f,
        0x4a, 0x56, 0x0b, 0xee, 0xa6, 0x4d, 0x9e, 0xa6, 0x69, 0xc1, 0x5b, 0x91,
        0x31, 0xc3, 0x59, 0x0e, 0x71, 0x09, 0x5b, 0xdf, 0x09, 0x55, 0xc4, 0xec,
        0x9b, 0x6c, 0xda, 0x73, 0x44, 0x67, 0x4c, 0x14, 0xe9, 0x58, 0x52, 0x1b,
        0xdb, 0x6e, 0x4c, 0x49, 0x10, 0x5d, 0x4e, 0x7f, 0x18, 0x61, 0xca, 0xbc,
        0x62, 0xeb, 0xca, 0x38, 0xcc, 0x97, 0x7c
    };

    const uint8_t encWithAadBytes[] = {
        0x8c, 0xc5, 0xb7, 0x16, 0x6a, 0x5c, 0x84, 0xc3, 0xb7, 0xf6, 0x4c, 0xb1, 
        0x01, 0x5e, 0xc0, 0x04, 0x03, 0xd8, 0xba, 0x30, 0xd9, 0x6d, 0x4c, 0x3d, 
        0xe0, 0x58, 0x5d, 0x5f, 0xfc, 0x4f, 0x12, 0xd1, 0xea, 0x69, 0x06, 0x28, 
        0x38, 0xbe, 0x3b, 0xf7, 0x81, 0x16, 0x3b, 0x8b, 0xc3, 0x13, 0xf9, 0x91, 
        0xb9, 0x79, 0x8f, 0x27, 0xd9, 0xf0, 0xdb, 0x74, 0x2f, 0x81, 0x82, 0x52, 
        0x14, 0xae, 0x2a, 0x3b, 0x98, 0x4d, 0xce, 0xd7, 0x78, 0x31, 0x7c, 0x69, 
        0x68, 0x4d, 0x67, 0x78, 0xf8, 0x61, 0x5f, 0xf1, 0x59, 0xa8, 0x38, 0x5c, 
        0x12, 0xa1, 0xe7, 0x30, 0x37, 0xa5, 0xdc, 0xbd, 0xf8, 0x50, 0x33, 0x53, 
        0x82, 0x82, 0xc7, 0x5f, 0x17, 0x83, 0x84, 0x17, 0xba, 0x5e, 0x58
    };

    const uint8_t encWithoutAadBytes[] = {
        0x8c, 0xc5, 0xb7, 0x16, 0x6a, 0x5c, 0x84, 0xc3, 0xb7, 0xf6, 0x4c, 0xb1, 
        0x01, 0x5e, 0xc0, 0x04, 0x03, 0xd8, 0xba, 0x30, 0xd9, 0x6d, 0x4c, 0x3d, 
        0xe0, 0x58, 0x5d, 0x5f, 0xfc, 0x4f, 0x12, 0xd1, 0xea, 0x69, 0x06, 0x28, 
        0x38, 0xbe, 0x3b, 0xf7, 0x81, 0x16, 0x3b, 0x8b, 0xc3, 0x13, 0xf9, 0x91, 
        0xb9, 0x79, 0x8f, 0x27, 0xd9, 0xf0, 0xdb, 0x74, 0x2f, 0x81, 0x82, 0x52, 
        0x14, 0xae, 0x2a, 0x3b, 0x98, 0x4d, 0xce, 0xd7, 0x78, 0x31, 0x7c, 0x69, 
        0x68, 0x4d, 0x67, 0x78, 0xf8, 0x61, 0x5f, 0xf1, 0x59, 0xa8, 0x38, 0x5c, 
        0x12, 0xa1, 0xe7, 0x30, 0x37, 0xa5, 0xdc, 0xb8, 0x3d, 0xa0, 0xd1, 0x66, 
        0x7a, 0x24, 0x96, 0x30, 0xd8, 0xe5, 0x7b, 0x37, 0xf0, 0xa4, 0x69
    };
    
    auto &crypto = crypto::getCryptoManager();
    auto crypter = crypto.newCipher(crypto::CipherAlgorithm::AES, crypto::CipherMode::GCM);
    ASSERT_TRUE(crypter);

    std::vector<uint8_t> enc;

    auto key = crypto.loadKey(keyBytes, sizeof(keyBytes));
    ASSERT_TRUE(key);

    {
        crypto::CipherIvParameters params(ivBytes, sizeof(ivBytes));

        // encrypt
        {
            {
                crypter->init(true, *key, &params);
                crypter->updateAAD(aadBytes, sizeof(aadBytes));
                
                enc = crypter->final(dataBytes, sizeof(dataBytes));

                ASSERT_TRUE(std::equal(encWithAadBytes, encWithAadBytes + sizeof(encWithAadBytes), enc.begin()));
            }

            {
                crypter->init(true, *key, &params);
                
                enc = crypter->final(dataBytes, sizeof(dataBytes));

                ASSERT_TRUE(std::equal(encWithoutAadBytes, encWithoutAadBytes + sizeof(encWithoutAadBytes), enc.begin()));
            }
        }

        // decrypt
        {
            {
                crypter->init(false, *key, &params);
                crypter->updateAAD(aadBytes, sizeof(aadBytes));
                    
                enc = crypter->final(encWithAadBytes, sizeof(encWithAadBytes));

                ASSERT_TRUE(std::equal(dataBytes, dataBytes + sizeof(dataBytes), enc.begin()));
            }

            {
                crypter->init(false, *key, &params);
                    
                enc = crypter->final(encWithoutAadBytes, sizeof(encWithoutAadBytes));

                ASSERT_TRUE(std::equal(dataBytes, dataBytes + sizeof(dataBytes), enc.begin()));
            }
        }
    }

    // Calling update byte by byte
    {
        crypto::CipherIvParameters params(ivBytes, sizeof(ivBytes));

        // encrypt
        {
            crypter->init(true, *key, &params);
            crypter->updateAAD(aadBytes, sizeof(aadBytes));
            
            enc.clear();

            for (const auto b : dataBytes) {
                auto e = crypter->update(&b, 1);
                enc.insert(enc.end(), e.begin(), e.end());
            }

            auto f = crypter->final();
            enc.insert(enc.end(), f.begin(), f.end());

            ASSERT_TRUE(std::equal(encWithAadBytes, encWithAadBytes + sizeof(encWithAadBytes), enc.begin()));
        }

        // decrypt
        {
            crypter->init(false, *key, &params);
            crypter->updateAAD(aadBytes, sizeof(aadBytes));
            
            enc.clear();

            for (const auto b : encWithAadBytes) {
                auto e = crypter->update(&b, 1);
                enc.insert(enc.end(), e.begin(), e.end());
            }

            auto f = crypter->final();
            enc.insert(enc.end(), f.begin(), f.end());

            ASSERT_TRUE(std::equal(dataBytes, dataBytes + sizeof(dataBytes), enc.begin()));
        }
    }
}
