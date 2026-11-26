// The NSURLSession transport for the one analytics request the app makes.
//
// One POST, no retry, a two second wait, and the answer reduced to "was it
// 2xx". Everything that could fail is a return value, not an exception: the
// caller runs on a detached thread at startup and a network problem must
// never reach the reader (PRIVACY.md, src/analytics.h).
//
// The payload is built by shared code and handed in; this file only knows
// how to put bytes on the wire on macOS.
//
// Sandboxing: NSURLSession needs no entitlement while the app is not
// sandboxed, which it is not today. A sandboxed build would need
// com.apple.security.network.client in its entitlements; without it the
// task fails with NSURLErrorNotConnectedToInternet and this transport
// reports false, which is the answer it gives anyway when the network is
// down. If the app is ever sandboxed, that entitlement has to be added.

#include "macos/telemetry_mac.h"

#include "platform/seams.h"

#import <Foundation/Foundation.h>

#include <cstdio>
#include <string>

#include "macos/crash_trace_mac.h"

namespace platform {
namespace {

// One request. NSURLSession is shared and thread safe, so a fresh object per
// call costs nothing and keeps no state between launches.
class UrlSessionTelemetrySink final : public TelemetrySink {
public:
    bool Post(const std::string& url, const std::string& appKey,
              const std::string& body) override {
        @autoreleasepool {
            NSString* urlString = [NSString stringWithUTF8String:url.c_str()];
            NSURL* nsUrl = urlString != nil
                               ? [NSURL URLWithString:urlString]
                               : nil;
            // The payload never travels in the clear.
            if (nsUrl == nil || ![[nsUrl scheme] isEqualToString:@"https"]) {
                TraceMac("analytics: refused a non https url");
                return false;
            }

            NSMutableURLRequest* request =
                [NSMutableURLRequest requestWithURL:nsUrl];
            [request setHTTPMethod:@"POST"];
            [request setValue:@"application/json"
                forHTTPHeaderField:@"Content-Type"];
            NSString* key = [NSString stringWithUTF8String:appKey.c_str()];
            if (key != nil) [request setValue:key forHTTPHeaderField:@"App-Key"];
            [request setHTTPBody:[NSData dataWithBytes:body.data()
                                                length:body.size()]];
            // The session's own timeout is a resource timeout; the request
            // timeout is the one that bounds a stuck connection.
            [request setTimeoutInterval:2.0];

            __block bool accepted = false;
            __block long status = 0;
            dispatch_semaphore_t done = dispatch_semaphore_create(0);
            NSURLSessionDataTask* task = [[NSURLSession sharedSession]
                dataTaskWithRequest:request
                  completionHandler:^(NSData* data, NSURLResponse* response,
                                      NSError* error) {
                      (void)data;
                      if (error == nil &&
                          [response isKindOfClass:[NSHTTPURLResponse class]]) {
                          status = (long)[(NSHTTPURLResponse*)response
                              statusCode];
                          accepted = status >= 200 && status < 300;
                      }
                      dispatch_semaphore_signal(done);
                  }];
            [task resume];

            // Two seconds, then give up. The completion handler signals the
            // semaphore, so the flag it wrote is visible after the wait.
            const dispatch_time_t deadline =
                dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC);
            if (dispatch_semaphore_wait(done, deadline) != 0) {
                [task cancel];
                TraceMac("analytics: app_start timed out");
                return false;
            }
            if (!accepted) {
                // The answer is the only diagnostic there is: Aptabase says
                // 4xx for a payload it rejects, and the app keeps working
                // either way.
                if (status != 0) {
                    TraceMac("analytics: app_start answered %ld", status);
                } else {
                    TraceMac("analytics: app_start failed");
                }
            }
            return accepted;
        }
    }
};

}  // namespace

std::unique_ptr<TelemetrySink> CreateTelemetrySink() {
    return std::unique_ptr<TelemetrySink>(new UrlSessionTelemetrySink());
}

std::string MacOsVersion() {
    @autoreleasepool {
        NSString* text = [[NSProcessInfo processInfo]
            operatingSystemVersionString];
        // "Version 15.1.1 (Build 24B91)" -> the second word, first two
        // components.
        NSArray<NSString*>* words =
            text != nil ? [text componentsSeparatedByString:@" "] : @[];
        if ([words count] >= 2) {
            NSArray<NSString*>* dotted =
                [words[1] componentsSeparatedByString:@"."];
            if ([dotted count] >= 1) {
                char buf[64];
                const long major = (long)[dotted[0] integerValue];
                const long minor =
                    [dotted count] >= 2 ? (long)[dotted[1] integerValue] : 0;
                std::snprintf(buf, sizeof(buf), "%ld.%ld", major, minor);
                return std::string(buf);
            }
        }
        // The structured answer, if the string ever changes shape.
        const NSOperatingSystemVersion version =
            [[NSProcessInfo processInfo] operatingSystemVersion];
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%ld.%ld", (long)version.majorVersion,
                      (long)version.minorVersion);
        return std::string(buf);
    }
}

std::string MacLocale() {
    @autoreleasepool {
        NSString* identifier = [[NSLocale currentLocale] localeIdentifier];
        if (identifier == nil) return std::string();
        const char* utf8 = [identifier UTF8String];
        if (utf8 == nullptr) return std::string();
        std::string tag(utf8);
        // "da_DK" -> "da-DK"; a calendar qualifier after '@' is not part of
        // the language tag the payload wants.
        const size_t at = tag.find('@');
        if (at != std::string::npos) tag.resize(at);
        for (char& c : tag) {
            if (c == '_') c = '-';
        }
        return tag;
    }
}

std::string MacAppVersion() {
    @autoreleasepool {
        NSString* version = [[NSBundle mainBundle]
            objectForInfoDictionaryKey:@"CFBundleShortVersionString"];
        if (version == nil) return "0.0.0";
        const char* utf8 = [version UTF8String];
        return utf8 != nullptr ? std::string(utf8) : std::string("0.0.0");
    }
}

analytics::SystemProps CurrentSystemProps() {
    analytics::SystemProps props;
    props.osName = "macOS";
    props.osVersion = MacOsVersion();
    props.appVersion = MacAppVersion();
    props.locale = MacLocale();
    props.debug = analytics::IsDebugBuild();
    return props;
}

}  // namespace platform
