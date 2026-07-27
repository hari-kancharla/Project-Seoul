// Project Seoul product runtime - macOS credential store.

#include "seoul/browser/product/browser/keychain_credential_store.h"

#import <Foundation/Foundation.h>
#import <Security/Security.h>

#include "base/command_line.h"
#include "components/os_crypt/common/os_crypt_switches.h"

namespace seoul {

namespace {

// One keychain service name for every Seoul credential; accounts are
// namespaced per profile inside it.
constexpr char kKeychainService[] = "Project Seoul";

NSString* ToNSString(const std::string& value) {
  return [[NSString alloc] initWithBytes:value.data()
                                  length:value.size()
                                encoding:NSUTF8StringEncoding];
}

NSMutableDictionary* BaseQuery(const std::string& account) {
  NSMutableDictionary* query = [NSMutableDictionary dictionary];
  query[(__bridge id)kSecClass] = (__bridge id)kSecClassGenericPassword;
  query[(__bridge id)kSecAttrService] = @(kKeychainService);
  query[(__bridge id)kSecAttrAccount] = ToNSString(account);
  return query;
}

}  // namespace

KeychainCredentialStore::KeychainCredentialStore(
    const std::string& profile_namespace)
    : profile_namespace_(profile_namespace),
      use_mock_keychain_(
          base::CommandLine::InitializedForCurrentProcess() &&
          base::CommandLine::ForCurrentProcess()->HasSwitch(
              os_crypt::switches::kUseMockKeychain)) {}

KeychainCredentialStore::~KeychainCredentialStore() = default;

std::string KeychainCredentialStore::QualifiedAccount(
    const std::string& account_key) const {
  return profile_namespace_ + "/" + account_key;
}

std::optional<std::string> KeychainCredentialStore::Get(
    const std::string& account_key) {
  if (account_key.empty()) {
    last_status_ = StoreStatus::kNotFound;
    return std::nullopt;
  }
  if (use_mock_keychain_) {
    const auto found = mock_secrets_.find(account_key);
    if (found == mock_secrets_.end()) {
      last_status_ = StoreStatus::kNotFound;
      return std::nullopt;
    }
    last_status_ = StoreStatus::kOk;
    return found->second;
  }
  NSMutableDictionary* query = BaseQuery(QualifiedAccount(account_key));
  query[(__bridge id)kSecReturnData] = @YES;
  query[(__bridge id)kSecMatchLimit] = (__bridge id)kSecMatchLimitOne;

  CFTypeRef result = nullptr;
  const OSStatus status =
      SecItemCopyMatching((__bridge CFDictionaryRef)query, &result);
  if (status == errSecItemNotFound) {
    last_status_ = StoreStatus::kNotFound;
    return std::nullopt;
  }
  if (status == errSecInteractionNotAllowed) {
    last_status_ = StoreStatus::kLocked;
    return std::nullopt;
  }
  if (status != errSecSuccess || !result) {
    last_status_ = StoreStatus::kUnavailable;
    return std::nullopt;
  }
  NSData* data = (__bridge_transfer NSData*)result;
  last_status_ = StoreStatus::kOk;
  return std::string(static_cast<const char*>(data.bytes), data.length);
}

bool KeychainCredentialStore::Set(const std::string& account_key,
                                  const std::string& secret) {
  if (account_key.empty() || secret.empty()) {
    return false;
  }
  if (use_mock_keychain_) {
    mock_secrets_.insert_or_assign(account_key, secret);
    last_status_ = StoreStatus::kOk;
    return true;
  }
  NSData* secret_data = [NSData dataWithBytes:secret.data()
                                       length:secret.size()];
  NSMutableDictionary* query = BaseQuery(QualifiedAccount(account_key));

  // Update in place when the item exists; add otherwise.
  NSDictionary* update = @{(__bridge id)kSecValueData : secret_data};
  OSStatus status = SecItemUpdate((__bridge CFDictionaryRef)query,
                                  (__bridge CFDictionaryRef)update);
  if (status == errSecItemNotFound) {
    query[(__bridge id)kSecValueData] = secret_data;
    // Accessible only when this Mac is unlocked, and never synced off-device.
    query[(__bridge id)kSecAttrAccessible] =
        (__bridge id)kSecAttrAccessibleWhenUnlockedThisDeviceOnly;
    status = SecItemAdd((__bridge CFDictionaryRef)query, nullptr);
  }
  if (status == errSecInteractionNotAllowed) {
    last_status_ = StoreStatus::kLocked;
    return false;
  }
  if (status != errSecSuccess) {
    last_status_ = StoreStatus::kUnavailable;
    return false;
  }
  last_status_ = StoreStatus::kOk;
  return true;
}

bool KeychainCredentialStore::Delete(const std::string& account_key) {
  if (account_key.empty()) {
    return false;
  }
  if (use_mock_keychain_) {
    if (mock_secrets_.erase(account_key) == 0) {
      last_status_ = StoreStatus::kNotFound;
      return false;
    }
    last_status_ = StoreStatus::kOk;
    return true;
  }
  NSMutableDictionary* query = BaseQuery(QualifiedAccount(account_key));
  const OSStatus status = SecItemDelete((__bridge CFDictionaryRef)query);
  if (status == errSecItemNotFound) {
    last_status_ = StoreStatus::kNotFound;
    return false;
  }
  if (status != errSecSuccess) {
    last_status_ = StoreStatus::kUnavailable;
    return false;
  }
  last_status_ = StoreStatus::kOk;
  return true;
}

}  // namespace seoul
