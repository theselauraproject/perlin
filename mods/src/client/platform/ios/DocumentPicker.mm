#include "client/platform/ios/DocumentPicker.hpp"

#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <filesystem>
#include <utility>

@interface PerlinDocumentPicker : NSObject <UIDocumentPickerDelegate> {
  @public
    std::function<void(std::vector<uint8_t>, std::string)> done;
}
@end

@implementation PerlinDocumentPicker
- (void)documentPicker:(UIDocumentPickerViewController*)controller didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls {
    for (NSURL* url in urls) {
        // asCopy: the file is already a private copy in our inbox, but iCloud items still need the scoped access
        const BOOL scoped = [url startAccessingSecurityScopedResource];
        NSData* data = [NSData dataWithContentsOfURL:url];
        if (scoped)
            [url stopAccessingSecurityScopedResource];
        if (!data || !done)
            continue;
        const auto* bytes = static_cast<const uint8_t*>(data.bytes);
        std::string ext = url.pathExtension.length ? std::string(".") + url.pathExtension.lowercaseString.UTF8String : "";
        done(std::vector<uint8_t>(bytes, bytes + data.length), std::move(ext));
    }
}
- (void)documentPickerWasCancelled:(UIDocumentPickerViewController*)controller {
}
@end

namespace perlin::ios {
    namespace {
        // the picker's delegate is weak: keep the current one alive until the next pick
        PerlinDocumentPicker* gDelegate = nil;

        UIViewController* TopController() {
            UIWindow* key = nil;
            for (UIScene* scene in UIApplication.sharedApplication.connectedScenes) {
                if (![scene isKindOfClass:[UIWindowScene class]])
                    continue;
                for (UIWindow* window in static_cast<UIWindowScene*>(scene).windows)
                    if (window.isKeyWindow || !key)
                        key = window;
            }
            UIViewController* top = key.rootViewController;
            while (top.presentedViewController)
                top = top.presentedViewController;
            return top;
        }

        NSArray<UTType*>* TypesFor(const std::string& kind) {
            if (kind == "skin" || kind == "cape")
                return @[ UTTypePNG ];
            // packs (.zip .mcpack .mcaddon .mcworld), mods (.wasm) and anything else: let the game sort it out
            return @[ UTTypeData, UTTypeZIP ];
        }
    }

    bool PickDocument(const std::string& kind, std::function<void(std::vector<uint8_t>, std::string)> done) {
        auto task = std::make_shared<std::function<void(std::vector<uint8_t>, std::string)>>(std::move(done));
        const std::string k = kind;
        dispatch_async(dispatch_get_main_queue(), ^{
          UIViewController* top = TopController();
          if (!top)
              return;
          UIDocumentPickerViewController* picker =
              [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:TypesFor(k) asCopy:YES];
          picker.allowsMultipleSelection = NO;
          gDelegate = [PerlinDocumentPicker new];
          gDelegate->done = *task;
          picker.delegate = gDelegate;
          [top presentViewController:picker animated:YES completion:nil];
        });
        return true;
    }
}
