#import <Foundation/Foundation.h>
#include "PlatformUtilsIOS.h"

namespace secure_db {

void PlatformUtilsIOS::applyStrictFileProtection(const std::string& filePath) {
    @autoreleasepool {
        NSString *path = [NSString stringWithUTF8String:filePath.c_str()];
        NSFileManager *fileManager = [NSFileManager defaultManager];

        if ([fileManager fileExistsAtPath:path]) {
            NSError *error = nil;
            NSDictionary *attributes = @{
                NSFileProtectionKey: NSFileProtectionComplete
            };
            BOOL success = [fileManager setAttributes:attributes ofItemAtPath:path error:&error];

            if (!success) {
                NSLog(@"[SecureDB] Failed to set FileProtectionComplete on %@: %@",
                      path, error.localizedDescription);
            } else {
                NSLog(@"[SecureDB] FileProtectionComplete applied to %@", path);
            }
        }
    }
}

void PlatformUtilsIOS::applyStrictFileProtectionToDirectory(const std::string& dirPath) {
    @autoreleasepool {
        NSString *path = [NSString stringWithUTF8String:dirPath.c_str()];
        NSFileManager *fileManager = [NSFileManager defaultManager];

        NSError *error = nil;
        NSArray *contents = [fileManager contentsOfDirectoryAtPath:path error:&error];

        if (error) {
            NSLog(@"[SecureDB] Failed to list directory contents: %@", error.localizedDescription);
            return;
        }

        for (NSString *file in contents) {
            NSString *fullPath = [path stringByAppendingPathComponent:file];
            applyStrictFileProtection(fullPath.UTF8String);
        }
    }
}

} // namespace secure_db